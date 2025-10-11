use core::ptr;
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use spin::Mutex;

// Optimized constants for performance
pub(crate) const HEAP_MAGIC_ALLOC: u32 = 0xDEADBEEF;
pub(crate) const HEAP_MAGIC_FREE: u32 = 0xFEEDFACE;
const MIN_BLOCK_SIZE: usize = 32;
const HEAP_ALIGN: usize = 8; // Match C heap alignment
const MAX_ALLOC_SIZE: usize = 1 << 30;
const NUM_SIZE_CLASSES: usize = 12; // Match C heap
const FAST_CACHE_SIZE: usize = 32;
const COALESCE_THRESHOLD: usize = 1000; // Match C heap frequency

// Simplified checksum computation
#[inline(always)]
fn compute_checksum(block: *const HeapBlock) -> u32 {
    unsafe {
        (block as usize ^ (*block).magic as usize ^ (*block).size) as u32
    }
}

// Size classes matching C heap for compatibility
pub static SIZE_CLASSES: [usize; NUM_SIZE_CLASSES] = [
    32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536
];

#[repr(C)]
pub struct HeapBlock {
    pub(crate) magic: u32,
    pub size: usize,
    pub is_free: u8,
    pub in_cache: u8,
    next: *mut HeapBlock,
    prev: *mut HeapBlock,
    checksum: u32,
    cache_next: *mut HeapBlock,
}

impl HeapBlock {
    #[inline(always)]
    pub(crate) fn is_free(&self) -> bool { self.is_free != 0 }
    
    #[inline(always)]
    pub(crate) fn set_free(&mut self, free: bool) {
        self.is_free = if free { 1 } else { 0 };
    }
    
    #[inline(always)]
    pub(crate) fn in_cache(&self) -> bool { self.in_cache != 0 }
    
    #[inline(always)]
    pub(crate) fn set_in_cache(&mut self, cached: bool) {
        self.in_cache = if cached { 1 } else { 0 };
    }
}

#[repr(C)]
#[derive(Copy, Clone)]
struct FastCache {
    free_list: *mut HeapBlock,
    count: i32,
    hits: u64,
    misses: u64,
}

#[repr(C)]
pub struct HeapStats {
    pub total_allocated: usize,
    pub peak_allocated: usize,
    pub alloc_count: u64,
    pub free_count: u64,
    pub cache_hits: u64,
    pub cache_misses: u64,
    pub coalesce_count: u64,
    pub corruption_count: u64,
}

unsafe impl Send for HeapState {}
unsafe impl Sync for HeapState {}

struct HeapState {
    head: *mut HeapBlock,
    fast_caches: [FastCache; NUM_SIZE_CLASSES],
    free_counter: usize,
}

// Lock-free counters
static TOTAL_ALLOCATED: AtomicUsize = AtomicUsize::new(0);
static PEAK_ALLOCATED: AtomicUsize = AtomicUsize::new(0);
static ALLOC_COUNTER: AtomicU64 = AtomicU64::new(0);
static FREE_COUNTER: AtomicU64 = AtomicU64::new(0);
static COALESCE_COUNTER: AtomicU64 = AtomicU64::new(0);
static CORRUPTION_COUNTER: AtomicU64 = AtomicU64::new(0);

static HEAP: Mutex<HeapState> = Mutex::new(HeapState {
    head: ptr::null_mut(),
    fast_caches: [FastCache {
        free_list: ptr::null_mut(),
        count: 0,
        hits: 0,
        misses: 0,
    }; NUM_SIZE_CLASSES],
    free_counter: 0,
});

extern "C" {
    fn VMemAlloc(size: u64) -> *mut u8;
    // fn VMemFree(ptr: *mut u8, size: u64);
    // fn PrintKernelError(msg: *const u8);
    // fn PrintKernelWarning(msg: *const u8);
}

impl HeapBlock {

    // Fast validation - only check magic for performance
    #[inline(always)]
    unsafe fn validate(&self) -> bool {
        let ptr = self as *const HeapBlock;
        !ptr.is_null() && (self.magic == HEAP_MAGIC_ALLOC || self.magic == HEAP_MAGIC_FREE)
    }

    // Simplified initialization for performance
    #[inline(always)]
    unsafe fn init(&mut self, size: usize, is_free: bool) {
        self.magic = if is_free { HEAP_MAGIC_FREE } else { HEAP_MAGIC_ALLOC };
        self.size = size;
        self.is_free = if is_free { 1 } else { 0 };
        self.in_cache = 0;
        self.cache_next = ptr::null_mut();
        self.checksum = compute_checksum(self as *const HeapBlock);
    }



    // Unsafe: This function performs pointer arithmetic.
    pub(crate) unsafe fn to_user_ptr(&self) -> *mut u8 {
        (self as *const HeapBlock as *mut u8).add(size_of::<HeapBlock>())
    }

    // Unsafe: This function performs pointer arithmetic.
    pub unsafe fn from_user_ptr(ptr: *mut u8) -> *mut HeapBlock {
        ptr.sub(size_of::<HeapBlock>()) as *mut HeapBlock
    }

    // Unsafe: This function performs pointer arithmetic.
    unsafe fn are_adjacent(&self, other: *const HeapBlock) -> bool {
        let self_end = (self as *const HeapBlock as *const u8)
            .add(size_of::<HeapBlock>())
            .add(self.size);
        self_end == other as *const u8
    }

    // Simplified coalescing
    unsafe fn coalesce_with_next(&mut self) -> bool {
        if self.next.is_null() || !(*self.next).is_free() || (*self.next).in_cache() {
            return false;
        }
        
        if !self.are_adjacent(self.next) { return false; }
        
        let next_block = self.next;
        self.size += size_of::<HeapBlock>() + (*next_block).size;
        self.next = (*next_block).next;
        
        if !self.next.is_null() {
            (*self.next).prev = self;
        }
        
        self.checksum = compute_checksum(self as *const HeapBlock);
        COALESCE_COUNTER.fetch_add(1, Ordering::Relaxed);
        true
    }
}

#[inline]
fn align_size(size: usize) -> usize {
    (size + HEAP_ALIGN - 1) & !(HEAP_ALIGN - 1)
}

// Fast size class lookup matching C implementation
#[inline(always)]
fn get_size_class(size: usize) -> Option<usize> {
    for i in 0..NUM_SIZE_CLASSES {
        if size <= SIZE_CLASSES[i] {
            return Some(i);
        }
    }
    None
}

// Simplified block creation matching C heap strategy
unsafe fn create_new_block(size: usize) -> *mut HeapBlock {
    let chunk_size = if size <= 1024 {
        if size < 4096 { 4096 } else { ((size * 4) + 4095) & !4095 }
    } else {
        size
    };

    let total_size = size_of::<HeapBlock>() + chunk_size;
    let mem = VMemAlloc(total_size as u64);
    if mem.is_null() {
        return ptr::null_mut();
    }

    let block = mem as *mut HeapBlock;
    (*block).init(chunk_size, false);

    // Link to head
    let mut heap = HEAP.lock();
    (*block).next = heap.head;
    (*block).prev = ptr::null_mut();
    if !heap.head.is_null() {
        (*heap.head).prev = block;
    }
    heap.head = block;
    drop(heap);

    // Split if much larger than needed
    if chunk_size > size {
        split_block(block, size);
    }

    block
}

// Unsafe: This function operates on raw pointers.
unsafe fn split_block(block: *mut HeapBlock, needed_size: usize) {
    let remaining = (*block).size - needed_size;
    if remaining < MIN_BLOCK_SIZE + size_of::<HeapBlock>() {
        return;
    }

    let new_block_ptr = (block as *mut u8)
        .add(size_of::<HeapBlock>())
        .add(needed_size) as *mut HeapBlock;

    let new_block = &mut *new_block_ptr;
    new_block.init(remaining - size_of::<HeapBlock>(), true);

    // Link new block
    new_block.next = (*block).next;
    new_block.prev = block;
    if !(*block).next.is_null() {
        (*(*block).next).prev = new_block_ptr;
    }
    (*block).next = new_block_ptr;

    // Update original block
    (*block).size = needed_size;
    (*block).checksum = compute_checksum(block as *const HeapBlock);
}

// Optimized free block search matching C heap strategy
unsafe fn find_free_block(size: usize) -> *mut HeapBlock {
    let heap = HEAP.lock();
    let mut current = heap.head;
    let mut best_fit = ptr::null_mut();
    let mut best_size = usize::MAX;
    let mut scanned = 0;
    let max_scan: usize = if size <= 1024 { 32 } else { usize::MAX };

    while !current.is_null() && scanned < max_scan {
        let block = &*current;
        if block.is_free() && !block.in_cache() && block.size >= size {
            if block.size == size { return current; }
            
            if size <= 1024 && block.size <= size * 2 {
                return current; // Close fit for small allocations
            }
            
            if block.size < best_size {
                best_fit = current;
                best_size = block.size;
            }
        }
        current = block.next;
        scanned += 1;
    }

    best_fit
}

// Simplified coalescing
unsafe fn coalesce_free_blocks() {
    let heap = HEAP.lock();
    let mut current = heap.head;

    while !current.is_null() {
        let block = &mut *current;
        if block.is_free() && !block.in_cache() {
            if block.coalesce_with_next() {
                continue;
            }
        }
        current = block.next;
    }
}

// Optimized allocation matching C heap performance
pub unsafe fn rust_kmalloc_backend(size: usize) -> *mut u8 {
    if size == 0 || size > MAX_ALLOC_SIZE {
        return ptr::null_mut();
    }

    let aligned_size = align_size(size.max(MIN_BLOCK_SIZE));
    let alloc_count = ALLOC_COUNTER.fetch_add(1, Ordering::Relaxed);

    // Periodic coalescing like C heap
    if (alloc_count % COALESCE_THRESHOLD as u64) == 0 {
        coalesce_free_blocks();
    }

    // Fast cache path
    if let Some(size_class) = get_size_class(aligned_size) {
        let actual_size = SIZE_CLASSES[size_class];
        let mut heap = HEAP.lock();
        let cache = &mut heap.fast_caches[size_class];
        
        if !cache.free_list.is_null() {
            let block = cache.free_list;
            cache.free_list = (*block).cache_next;
            cache.count -= 1;
            cache.hits += 1;
            drop(heap);

            (*block).init(actual_size, false);
            let new_total = TOTAL_ALLOCATED.fetch_add(actual_size, Ordering::Relaxed) + actual_size;
            update_peak(new_total);
            return (*block).to_user_ptr();
        }
        cache.misses += 1;
        drop(heap);
    }

    // Standard allocation path
    let block = find_free_block(aligned_size);
    let block = if !block.is_null() {
        if !(*block).validate() { return ptr::null_mut(); }
        
        if (*block).size > aligned_size {
            split_block(block, aligned_size);
        }
        (*block).init(aligned_size, false);
        block
    } else {
        create_new_block(aligned_size)
    };

    if block.is_null() {
        return ptr::null_mut();
    }

    let new_total = TOTAL_ALLOCATED.fetch_add((*block).size, Ordering::Relaxed) + (*block).size;
    update_peak(new_total);
    (*block).to_user_ptr()
}

#[inline]
fn update_peak(new_total: usize) {
    let mut peak = PEAK_ALLOCATED.load(Ordering::Relaxed);
    if new_total > peak {
        // Only attempt CAS if we might actually update
        loop {
            match PEAK_ALLOCATED.compare_exchange_weak(
                peak, new_total, Ordering::Relaxed, Ordering::Relaxed
            ) {
                Ok(_) => break,
                Err(current) => {
                    peak = current;
                    if new_total <= peak {
                        break; // Someone else updated to higher value
                    }
                }
            }
        }
    }
}

// Optimized free matching C heap
pub unsafe fn rust_kfree_backend(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }

    let block = HeapBlock::from_user_ptr(ptr);

    if (*block).magic != HEAP_MAGIC_ALLOC || !(*block).validate() {
        return;
    }

    if (*block).is_free() {
        return; // Double free protection
    }

    let block_size = (*block).size;
    TOTAL_ALLOCATED.fetch_sub(block_size, Ordering::Relaxed);
    FREE_COUNTER.fetch_add(1, Ordering::Relaxed);

    // Try fast cache first for small allocations
    if let Some(size_class) = get_size_class(block_size) {
        if block_size == SIZE_CLASSES[size_class] {
            // Zero user data for security
            ptr::write_bytes(ptr, 0, block_size);
            
            let mut heap = HEAP.lock();
            let cache = &mut heap.fast_caches[size_class];
            if cache.count < FAST_CACHE_SIZE as i32 {
                (*block).init(block_size, true);
                (*block).cache_next = cache.free_list;
                cache.free_list = block;
                cache.count += 1;
                (*block).set_in_cache(true);
                return;
            }
        }
    }

    // Standard free path with coalescing
    ptr::write_bytes(ptr, 0, block_size);
    (*block).init(block_size, true);
    
    // Coalesce with adjacent blocks
    let current = block;
    while (*current).coalesce_with_next() {}
    
    // Try coalescing with previous
    if !(*current).prev.is_null() && (*(*current).prev).is_free() {
        let prev = (*current).prev;
        if (*prev).are_adjacent(current) {
            (*prev).size += size_of::<HeapBlock>() + (*current).size;
            (*prev).next = (*current).next;
            if !(*current).next.is_null() {
                (*(*current).next).prev = prev;
            }
            (*prev).checksum = compute_checksum(prev as *const HeapBlock);
        }
    }
}

// Unsafe: This function calls other unsafe functions.
pub unsafe fn rust_krealloc_backend(ptr: *mut u8, new_size: usize) -> *mut u8 {
    if ptr.is_null() {
        return rust_kmalloc_backend(new_size);
    }

    if new_size == 0 {
        rust_kfree_backend(ptr);
        return ptr::null_mut();
    }

    let block = HeapBlock::from_user_ptr(ptr);
    if !(*block).validate() || (*block).magic != HEAP_MAGIC_ALLOC {
        return ptr::null_mut();
    }

    let old_size = (*block).size;
    let aligned_new_size = align_size(new_size.max(16));

    // If shrinking or size is close enough, reuse
    if aligned_new_size <= old_size && old_size <= aligned_new_size * 2 {
        return ptr;
    }

    let new_ptr = rust_kmalloc_backend(new_size);
    if !new_ptr.is_null() {
        let copy_size = old_size.min(new_size).saturating_sub(8); // Account for canary
        ptr::copy_nonoverlapping(ptr, new_ptr, copy_size);
        rust_kfree_backend(ptr);
    }

    new_ptr
}

// Unsafe: This function calls other unsafe functions.
pub unsafe fn rust_kcalloc_backend(count: usize, size: usize) -> *mut u8 {
    if count == 0 || size == 0 {
        return ptr::null_mut();
    }
    let total_size = match count.checked_mul(size) {
        Some(v) => v,
        None => return ptr::null_mut(),
    };
    let ptr = rust_kmalloc_backend(total_size);
    if !ptr.is_null() {
        ptr::write_bytes(ptr, 0, total_size);
    }
    ptr
}

#[no_mangle]
pub extern "C" fn rust_heap_get_stats(stats: *mut HeapStats) {
    if stats.is_null() {
        return;
    }

    // Unsafe: This block dereferences a raw pointer `stats`.
    unsafe {
        let heap = HEAP.lock();
        let mut total_hits = 0;
        let mut total_misses = 0;

        for cache in &heap.fast_caches {
            total_hits += cache.hits;
            total_misses += cache.misses;
        }

        *stats = HeapStats {
            total_allocated: TOTAL_ALLOCATED.load(Ordering::Relaxed),
            peak_allocated: PEAK_ALLOCATED.load(Ordering::Relaxed),
            alloc_count: ALLOC_COUNTER.load(Ordering::Relaxed),
            free_count: FREE_COUNTER.load(Ordering::Relaxed),
            cache_hits: total_hits,
            cache_misses: total_misses,
            coalesce_count: COALESCE_COUNTER.load(Ordering::Relaxed),
            corruption_count: CORRUPTION_COUNTER.load(Ordering::Relaxed),
        };
    }
}

#[no_mangle]
pub extern "C" fn rust_heap_validate() -> i32 {
    // Unsafe: This block dereferences raw pointers while traversing the heap.
    unsafe {
        let heap = HEAP.lock();
        let mut current = heap.head;
        let mut errors = 0;

        while !current.is_null() {
            if !(*current).validate() {
                errors += 1;
            }
            current = (*current).next;
        }

        errors
    }
}