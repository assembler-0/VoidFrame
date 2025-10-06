use core::ptr;
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use spin::Mutex;

// Production constants
const HEAP_MAGIC_ALLOC: u32 = 0xDEADBEEF;
const HEAP_MAGIC_FREE: u32 = 0xFEEDFACE;
const MIN_BLOCK_SIZE: usize = 32;
const HEAP_ALIGN: usize = 16; // Better alignment for SIMD
const MAX_ALLOC_SIZE: usize = 1 << 30;
const NUM_SIZE_CLASSES: usize = 16;
const FAST_CACHE_SIZE: usize = 64; // Larger cache
const CANARY_VALUE: u64 = 0xDEADC0DEDEADBEEF;
const POISON_VALUE: u8 = 0xDE;
const COALESCE_THRESHOLD: usize = 256; // Coalesce every N frees

// Optimized size classes with better coverage
static SIZE_CLASSES: [usize; NUM_SIZE_CLASSES] = [
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096
];

#[repr(C)]
pub struct HeapBlock {
    magic: u32,
    pub size: usize,
    is_free: u8,
    in_cache: u8,
    next: *mut HeapBlock,
    prev: *mut HeapBlock,
    checksum: u32,
    cache_next: *mut HeapBlock,
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
    fn VMemFree(ptr: *mut u8, size: u64);
    fn PrintKernelError(msg: *const u8);
    fn PrintKernelWarning(msg: *const u8);
}

impl HeapBlock {
    unsafe fn compute_checksum(&self) -> u32 {
        (self as *const _ as usize ^ self.magic as usize ^ self.size) as u32
    }

    unsafe fn validate(&self) -> bool {
        if (self as *const HeapBlock).is_null() {
            return false;
        }
        
        if self.magic != HEAP_MAGIC_ALLOC && self.magic != HEAP_MAGIC_FREE {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
            return false;
        }
        
        if self.size == 0 || self.size > MAX_ALLOC_SIZE {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
            return false;
        }
        
        if self.checksum != self.compute_checksum() {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
            return false;
        }
        
        true
    }

    unsafe fn init(&mut self, size: usize, is_free: bool) {
        self.magic = if is_free { HEAP_MAGIC_FREE } else { HEAP_MAGIC_ALLOC };
        self.size = size;
        self.is_free = if is_free { 1 } else { 0 };
        self.in_cache = 0;
        self.cache_next = ptr::null_mut();
        self.checksum = self.compute_checksum();
        
        // Add canary for allocated blocks
        if !is_free && size >= 16 {
            let canary_ptr = self.to_user_ptr().add(size - 8) as *mut u64;
            *canary_ptr = CANARY_VALUE;
        }
    }
    
    unsafe fn validate_canary(&self) -> bool {
        if self.is_free != 0 || self.size < 16 { 
            return true; 
        }
        let canary_ptr = self.to_user_ptr().add(self.size - 8) as *const u64;
        let valid = *canary_ptr == CANARY_VALUE;
        if !valid {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
        }
        valid
    }

    unsafe fn to_user_ptr(&self) -> *mut u8 {
        (self as *const HeapBlock as *mut u8).add(core::mem::size_of::<HeapBlock>())
    }

    pub unsafe fn from_user_ptr(ptr: *mut u8) -> *mut HeapBlock {
        ptr.sub(core::mem::size_of::<HeapBlock>()) as *mut HeapBlock
    }
    
    unsafe fn are_adjacent(&self, other: *const HeapBlock) -> bool {
        let self_end = (self as *const HeapBlock as *const u8)
            .add(core::mem::size_of::<HeapBlock>())
            .add(self.size);
        self_end == other as *const u8
    }
    
    unsafe fn coalesce_with_next(&mut self) -> bool {
        if self.next.is_null() || (*self.next).is_free == 0 || (*self.next).in_cache != 0 {
            return false;
        }
        
        if !self.are_adjacent(self.next) {
            return false;
        }
        
        let next_block = self.next;
        self.size += core::mem::size_of::<HeapBlock>() + (*next_block).size;
        self.next = (*next_block).next;
        
        if !self.next.is_null() {
            (*self.next).prev = self;
        }
        
        self.checksum = self.compute_checksum();
        COALESCE_COUNTER.fetch_add(1, Ordering::Relaxed);
        true
    }
}

#[inline]
fn align_size(size: usize) -> usize {
    (size + HEAP_ALIGN - 1) & !(HEAP_ALIGN - 1)
}

fn get_size_class(size: usize) -> Option<usize> {
    // Binary search for better performance
    let mut left = 0;
    let mut right = SIZE_CLASSES.len();
    
    while left < right {
        let mid = (left + right) / 2;
        if SIZE_CLASSES[mid] >= size {
            right = mid;
        } else {
            left = mid + 1;
        }
    }
    
    if left < SIZE_CLASSES.len() {
        Some(left)
    } else {
        None
    }
}

unsafe fn create_new_block(size: usize) -> *mut HeapBlock {
    // Optimized chunk sizing for better memory utilization
    let chunk_size = if size <= 1024 {
        // Small allocations: allocate in 4KB chunks
        let chunks_needed = (size + 4095) / 4096;
        chunks_needed * 4096
    } else if size <= 65536 {
        // Medium allocations: round to next 4KB boundary
        (size + 4095) & !4095
    } else {
        // Large allocations: round to 64KB boundary for better alignment
        (size + 65535) & !65535
    };

    let total_size = core::mem::size_of::<HeapBlock>() + chunk_size;
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
    drop(heap); // Release lock early

    // Split if significantly larger than needed
    if chunk_size > size * 3 && chunk_size - size >= MIN_BLOCK_SIZE + core::mem::size_of::<HeapBlock>() {
        split_block(block, size);
    }

    block
}

unsafe fn split_block(block: *mut HeapBlock, needed_size: usize) {
    let remaining = (*block).size - needed_size;
    if remaining < MIN_BLOCK_SIZE + core::mem::size_of::<HeapBlock>() {
        return;
    }

    let new_block_ptr = (block as *mut u8)
        .add(core::mem::size_of::<HeapBlock>())
        .add(needed_size) as *mut HeapBlock;
    
    let new_block = &mut *new_block_ptr;
    new_block.init(remaining - core::mem::size_of::<HeapBlock>(), true);
    
    // Link new block
    new_block.next = (*block).next;
    new_block.prev = block;
    if !(*block).next.is_null() {
        (*(*block).next).prev = new_block_ptr;
    }
    (*block).next = new_block_ptr;
    
    // Update original block
    (*block).size = needed_size;
    (*block).checksum = (*block).compute_checksum();
}

unsafe fn find_free_block(size: usize) -> *mut HeapBlock {
    let heap = HEAP.lock();
    let mut current = heap.head;
    let mut scanned = 0;
    const MAX_SCAN: usize = 32; // Reduced for better performance

    // First-fit with early termination for good matches
    while !current.is_null() && scanned < MAX_SCAN {
        let block = &*current;
        if block.is_free != 0 && block.in_cache == 0 && block.size >= size {
            // Accept if size is reasonable (within 4x)
            if block.size <= size * 4 {
                return current;
            }
        }
        current = block.next;
        scanned += 1;
    }

    ptr::null_mut()
}

unsafe fn coalesce_free_blocks() {
    let heap = HEAP.lock();
    let mut current = heap.head;
    let mut coalesced = 0;
    
    while !current.is_null() && coalesced < 32 {
        let block = &mut *current;
        if block.is_free != 0 && block.in_cache == 0 {
            if block.coalesce_with_next() {
                coalesced += 1;
                continue; // Don't advance, check same block again
            }
        }
        current = block.next;
    }
}

pub unsafe fn rust_kmalloc_backend(size: usize) -> *mut u8 {
    if size == 0 || size > MAX_ALLOC_SIZE {
        return ptr::null_mut();
    }

    let aligned_size = align_size(size.max(16));
    ALLOC_COUNTER.fetch_add(1, Ordering::Relaxed);

    // Fast cache path - optimized for common case
    if let Some(size_class) = get_size_class(aligned_size) {
        let mut heap = HEAP.lock();
        let cache = &mut heap.fast_caches[size_class];
        if !cache.free_list.is_null() {
            let block = cache.free_list;
            cache.free_list = (*block).cache_next;
            cache.count -= 1;
            cache.hits += 1;
            drop(heap); // Release lock early

            // Validate after lock release for better concurrency
            if !(*block).validate() {
                CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
                return ptr::null_mut();
            }
            
            (*block).cache_next = ptr::null_mut();
            (*block).in_cache = 0;
            (*block).is_free = 0;
            (*block).magic = HEAP_MAGIC_ALLOC;
            (*block).checksum = (*block).compute_checksum();

            let new_total = TOTAL_ALLOCATED.fetch_add((*block).size, Ordering::Relaxed) + (*block).size;
            update_peak(new_total);

            return (*block).to_user_ptr();
        }
        cache.misses += 1;
    }

    // Slow path - find existing or create new
    let block = find_free_block(aligned_size);
    let block = if !block.is_null() {
        if !(*block).validate() {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
            return ptr::null_mut();
        }
        (*block).is_free = 0;
        (*block).magic = HEAP_MAGIC_ALLOC;
        (*block).checksum = (*block).compute_checksum();
        
        if (*block).size > aligned_size * 2 {
            split_block(block, aligned_size);
        }
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

pub unsafe fn rust_kfree_backend(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }

    let block = HeapBlock::from_user_ptr(ptr);
    
    // Fast validation path
    if (*block).magic != HEAP_MAGIC_ALLOC || !(*block).validate() {
        CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
        return;
    }

    if !(*block).validate_canary() {
        CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
        return;
    }

    let block_size = (*block).size;
    
    // Poison memory (skip canary area)
    ptr::write_bytes(ptr, POISON_VALUE, block_size.saturating_sub(8));

    (*block).is_free = 1;
    (*block).magic = HEAP_MAGIC_FREE;
    (*block).checksum = (*block).compute_checksum();

    TOTAL_ALLOCATED.fetch_sub(block_size, Ordering::Relaxed);
    FREE_COUNTER.fetch_add(1, Ordering::Relaxed);
    
    // Try fast cache first
    if let Some(size_class) = get_size_class(block_size) {
        let mut heap = HEAP.lock();
        let cache = &mut heap.fast_caches[size_class];
        if cache.count < FAST_CACHE_SIZE as i32 {
            (*block).cache_next = cache.free_list;
            cache.free_list = block;
            cache.count += 1;
            (*block).in_cache = 1;
            return;
        }
        
        // Check for periodic coalescing
        heap.free_counter += 1;
        let should_coalesce = heap.free_counter >= COALESCE_THRESHOLD;
        if should_coalesce {
            heap.free_counter = 0;
        }
        drop(heap);
        
        if should_coalesce {
            coalesce_free_blocks();
        }
    }
}

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
        core::ptr::copy_nonoverlapping(ptr, new_ptr, copy_size);
        rust_kfree_backend(ptr);
    }

    new_ptr
}

pub unsafe fn rust_kcalloc_backend(count: usize, size: usize) -> *mut u8 {
    let total_size = count.saturating_mul(size);
    if total_size / count != size { // Overflow check
        return ptr::null_mut();
    }
    
    let ptr = rust_kmalloc_backend(total_size);
    if !ptr.is_null() {
        core::ptr::write_bytes(ptr, 0, total_size);
    }
    ptr
}

#[no_mangle]
pub extern "C" fn rust_heap_get_stats(stats: *mut HeapStats) {
    if stats.is_null() {
        return;
    }
    
    unsafe {
        let heap = HEAP.lock();
        let mut total_hits = 0;
        let mut total_misses = 0;
        
        for cache in &heap.fast_caches {
            total_hits += cache.hits;
            total_misses += cache.misses;
        }
        
        (*stats) = HeapStats {
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