use core::ptr;
use core::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use spin::Mutex;

// Security-hardened constants
pub(crate) const HEAP_MAGIC_ALLOC: u32 = 0xA110CA7E;
pub(crate) const HEAP_MAGIC_FREE: u32 = 0xF4EE1157;
const MIN_BLOCK_SIZE: usize = 32;
const HEAP_ALIGN: usize = 32; // AVX2 alignment
const MAX_ALLOC_SIZE: usize = 1 << 28; // Reduced for safety
const NUM_SIZE_CLASSES: usize = 16;
const FAST_CACHE_SIZE: usize = 32; // Reduced for better cache locality
pub(crate) const POISON_VALUE: u8 = 0xCC; // INT3 instruction
const LARGE_ALLOC_THRESHOLD: usize = 4096;
const COALESCE_THRESHOLD: usize = 128; // More frequent coalescing

// Compute a unique canary for a given block address
#[inline(always)]
fn compute_canary(addr: usize) -> u64 {
    // A simple but effective mix of the address with a secret constant
    (addr as u64).wrapping_mul(0x5AFE6AAD5AFE6AAD) ^ 0xDEADBEEFDEADBEEF
}

// Optimized size classes with better coverage
static SIZE_CLASSES: [usize; NUM_SIZE_CLASSES] = [
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096
];

#[repr(C, align(32))]
pub struct HeapBlock {
    pub(crate) magic: u32,
    checksum: u32,
    pub size: usize,
    flags: u8, // Combined is_free and in_cache
    _pad: [u8; 7], // Explicit padding for alignment
    next: *mut HeapBlock,
    prev: *mut HeapBlock,
    cache_next: *mut HeapBlock,
}

impl HeapBlock {
    #[inline(always)]
    pub(crate) fn is_free(&self) -> bool { self.flags & 1 != 0 }
    
    #[inline(always)]
    pub(crate) fn set_free(&mut self, free: bool) {
        if free { self.flags |= 1; } else { self.flags &= !1; }
    }
    
    #[inline(always)]
    pub(crate) fn in_cache(&self) -> bool { self.flags & 2 != 0 }
    
    #[inline(always)]
    pub(crate) fn set_in_cache(&mut self, cached: bool) {
        if cached { self.flags |= 2; } else { self.flags &= !2; }
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
    fn VMemFree(ptr: *mut u8, size: u64);
    fn PrintKernelError(msg: *const u8);
    fn PrintKernelWarning(msg: *const u8);
}

const FNV_PRIME: u32 = 16777619;
const FNV_OFFSET_BASIS: u32 = 2166136261;

impl HeapBlock {
    // Unsafe: This function operates on raw pointers and assumes the block is valid.
    unsafe fn compute_checksum(&self) -> u32 {
        let mut hash = FNV_OFFSET_BASIS;
        let addr = self as *const _ as usize;
        
        // Hash address and core metadata
        hash = (hash ^ (addr as u32)).wrapping_mul(FNV_PRIME);
        hash = (hash ^ self.magic).wrapping_mul(FNV_PRIME);
        hash = (hash ^ (self.size as u32)).wrapping_mul(FNV_PRIME);
        hash = (hash ^ (self.flags as u32)).wrapping_mul(FNV_PRIME);

        hash
    }

    // Unsafe: This function operates on raw pointers and assumes the block is valid.
    unsafe fn validate(&self) -> bool {
        // Fast null check
        let ptr = self as *const HeapBlock;
        if ptr.is_null() { return false; }
        
        // Batch validation for better performance
        let addr = self as *const HeapBlock as usize;
        let valid_magic = self.magic == HEAP_MAGIC_ALLOC || self.magic == HEAP_MAGIC_FREE;
        let valid_align = (addr & (HEAP_ALIGN - 1)) == 0;
        let valid_size = self.size > 0 && self.size <= MAX_ALLOC_SIZE && (self.size & (HEAP_ALIGN - 1)) == 0;
        let valid_checksum = self.checksum == self.compute_checksum();
        
        let is_valid = valid_magic && valid_align && valid_size && valid_checksum;
        
        if !is_valid {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
        }
        
        is_valid
    }

    // Unsafe: This function operates on raw pointers and assumes the block is valid.
    unsafe fn init(&mut self, size: usize, is_free: bool) {
        self.magic = if is_free { HEAP_MAGIC_FREE } else { HEAP_MAGIC_ALLOC };
        self.size = size;
        self.set_free(is_free);
        self.set_in_cache(false);
        self.cache_next = ptr::null_mut();
        
        // Add canary for allocated blocks
        if !is_free && size >= 16 {
            let canary_ptr = self.to_user_ptr().add(size - 8) as *mut u64;
            // Unsafe: Writing to a raw pointer.
            *canary_ptr = compute_canary(self as *const _ as usize);
        }
        
        // Poison free blocks with secure pattern
        if is_free {
            let poison_size = size.min(512); // Increased poison size
            // Unsafe: Writing to a raw pointer.
            ptr::write_bytes(self.to_user_ptr(), POISON_VALUE, poison_size);
        }
        
        self.checksum = self.compute_checksum();
    }

    // Unsafe: This function operates on raw pointers and assumes the block is valid.
    unsafe fn validate_canary(&self) -> bool {
        if self.is_free() || self.size < 16 { return true; }
        
        let canary_ptr = self.to_user_ptr().add(self.size - 8) as *const u64;
        let expected_canary = compute_canary(self as *const _ as usize);
        // Unsafe: Reading from a raw pointer.
        let valid = *canary_ptr == expected_canary;
        
        if !valid {
            CORRUPTION_COUNTER.fetch_add(1, Ordering::Relaxed);
        }
        
        valid
    }

    // Unsafe: This function performs pointer arithmetic.
    pub(crate) unsafe fn to_user_ptr(&self) -> *mut u8 {
        (self as *const HeapBlock as *mut u8).add(core::mem::size_of::<HeapBlock>())
    }

    // Unsafe: This function performs pointer arithmetic.
    pub unsafe fn from_user_ptr(ptr: *mut u8) -> *mut HeapBlock {
        ptr.sub(core::mem::size_of::<HeapBlock>()) as *mut HeapBlock
    }

    // Unsafe: This function performs pointer arithmetic.
    unsafe fn are_adjacent(&self, other: *const HeapBlock) -> bool {
        let self_end = (self as *const HeapBlock as *const u8)
            .add(core::mem::size_of::<HeapBlock>())
            .add(self.size);
        self_end == other as *const u8
    }

    // Unsafe: This function operates on raw pointers and assumes the block is valid.
    unsafe fn coalesce_with_next(&mut self) -> bool {
        if self.next.is_null() || !(*self.next).is_free() || (*self.next).in_cache() {
            return false;
        }
        
        if !self.are_adjacent(self.next) { return false; }
        
        let next_block = self.next;
        self.size += core::mem::size_of::<HeapBlock>() + (*next_block).size;
        self.next = (*next_block).next;
        
        if !self.next.is_null() {
            (*self.next).prev = self;
        }
        
        // Clear the coalesced block's magic for security
        (*next_block).magic = 0;
        
        self.checksum = self.compute_checksum();
        COALESCE_COUNTER.fetch_add(1, Ordering::Relaxed);
        true
    }
}

#[inline]
fn align_size(size: usize) -> usize {
    (size + HEAP_ALIGN - 1) & !(HEAP_ALIGN - 1)
}

#[inline(always)]
fn get_size_class(size: usize) -> Option<usize> {
    if size > 4096 {
        return None;
    }
    
    // Fast path for small sizes using a lookup table approach
    if size <= 64 {
        return Some((size.saturating_sub(1)) / 16);
    }
    
    // For larger sizes, use leading zero count for a fast log2 approximation
    let log2_size = (core::mem::size_of::<usize>() * 8) as u32 - size.leading_zeros() - 1;
    
    match log2_size {
        6 => Some(4),  // 65-128
        7 => Some(5),  // 129-256
        8 => Some(6),  // 257-512
        9 => Some(7),  // 513-1024
        10 => Some(8), // 1025-2048
        11 => Some(9), // 2049-4096
        _ => None
    }
}

// Unsafe: This function calls VMemAlloc and operates on raw pointers.
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
    // Unsafe: VMemAlloc is an external C function.
    let mem = VMemAlloc(total_size as u64);
    if mem.is_null() {
        return ptr::null_mut();
    }

    // Zero the memory to prevent info leaks
    // Unsafe: Writing to a raw pointer.
    ptr::write_bytes(mem, 0, total_size);

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

// Unsafe: This function operates on raw pointers.
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

// Unsafe: This function operates on raw pointers.
unsafe fn find_free_block(size: usize) -> *mut HeapBlock {
    let heap = HEAP.lock();
    let mut current = heap.head;
    let mut best_fit = ptr::null_mut();
    let mut best_size = usize::MAX;
    let mut scanned = 0;
    const MAX_SCAN: usize = 8; // Reduced for better latency

    while !current.is_null() && scanned < MAX_SCAN {
        let block = &*current;
        if block.is_free() && !block.in_cache() && block.size >= size {
            // Perfect fit - return immediately
            if block.size == size { return current; }
            
            // Better fit heuristic
            if block.size < best_size && block.size <= size.saturating_mul(3) / 2 {
                best_fit = current;
                best_size = block.size;
            }
        }
        current = block.next;
        scanned += 1;
    }

    best_fit
}

// Unsafe: This function operates on raw pointers.
unsafe fn coalesce_free_blocks() {
    let heap = HEAP.lock();
    let mut current = heap.head;
    let mut coalesced = 0;

    while !current.is_null() && coalesced < 16 {
        let block = &mut *current;
        if block.is_free() && !block.in_cache() {
            if block.coalesce_with_next() {
                coalesced += 1;
                continue;
            }
        }
        current = block.next;
    }
}

// Unsafe: This function calls other unsafe functions.
pub unsafe fn rust_kmalloc_backend(size: usize) -> *mut u8 {
    if size == 0 || size > MAX_ALLOC_SIZE {
        return ptr::null_mut();
    }

    let aligned_size = align_size(size.max(16));
    ALLOC_COUNTER.fetch_add(1, Ordering::Relaxed);

    // Fast cache path
    if let Some(size_class) = get_size_class(aligned_size) {
        let mut heap = HEAP.lock();
        let cache = &mut heap.fast_caches[size_class];
        if !cache.free_list.is_null() {
            let block = cache.free_list;
            cache.free_list = (*block).cache_next;
            cache.count -= 1;
            cache.hits += 1;
            drop(heap);

            if !(*block).validate() {
                return ptr::null_mut();
            }

            (*block).cache_next = ptr::null_mut();
            (*block).set_in_cache(false);
            (*block).set_free(false);
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
        if !(*block).validate() { return ptr::null_mut(); }
        
        (*block).set_free(false);
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

// Unsafe: This function calls other unsafe functions.
pub unsafe fn rust_kfree_backend(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }

    let block = HeapBlock::from_user_ptr(ptr);

    // Fast validation
    if (*block).magic != HEAP_MAGIC_ALLOC || !(*block).validate() || !(*block).validate_canary() {
        return;
    }

    let block_size = (*block).size;

    // Secure memory clearing
    ptr::write_bytes(ptr, POISON_VALUE, block_size.saturating_sub(8));

    (*block).set_free(true);
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
            (*block).set_in_cache(true);
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
        core::ptr::copy_nonoverlapping(ptr, new_ptr, copy_size);
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
        core::ptr::write_bytes(ptr, 0, total_size);
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