use core::ptr;
use spin::Mutex;

// Constants matching the C implementation
const HEAP_MAGIC_ALLOC: u32 = 0xDEADBEEF;
const HEAP_MAGIC_FREE: u32 = 0xFEEDFACE;
const MIN_BLOCK_SIZE: usize = 32;
const HEAP_ALIGN: usize = 8;
const MAX_ALLOC_SIZE: usize = 1 << 30;
const NUM_SIZE_CLASSES: usize = 12;
const FAST_CACHE_SIZE: usize = 32;

static SIZE_CLASSES: [usize; NUM_SIZE_CLASSES] = [
    32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536
];

#[repr(C)]
struct HeapBlock {
    magic: u32,
    size: usize,
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

unsafe impl Send for HeapState {}
unsafe impl Sync for HeapState {}

struct HeapState {
    head: *mut HeapBlock,
    total_allocated: usize,
    peak_allocated: usize,
    fast_caches: [FastCache; NUM_SIZE_CLASSES],
    alloc_counter: u64,
    validation_level: i32,
}

static HEAP: Mutex<HeapState> = Mutex::new(HeapState {
    head: ptr::null_mut(),
    total_allocated: 0,
    peak_allocated: 0,
    fast_caches: [FastCache {
        free_list: ptr::null_mut(),
        count: 0,
        hits: 0,
        misses: 0,
    }; NUM_SIZE_CLASSES],
    alloc_counter: 0,
    validation_level: 1,
});

extern "C" {
    fn VMemAlloc(size: u64) -> *mut u8;
    fn VMemFree(ptr: *mut u8, size: u64);
    fn PrintKernelError(msg: *const u8);
}

impl HeapBlock {
    unsafe fn compute_checksum(&self) -> u32 {
        (self as *const _ as usize ^ self.magic as usize ^ self.size) as u32
    }

    unsafe fn validate_fast(&self) -> bool {
        !self.is_null() && (self.magic == HEAP_MAGIC_ALLOC || self.magic == HEAP_MAGIC_FREE)
    }

    unsafe fn is_null(&self) -> bool {
        (self as *const HeapBlock).is_null()
    }

    unsafe fn init(&mut self, size: usize, is_free: bool) {
        self.magic = if is_free { HEAP_MAGIC_FREE } else { HEAP_MAGIC_ALLOC };
        self.size = size;
        self.is_free = if is_free { 1 } else { 0 };
        self.in_cache = 0;
        self.cache_next = ptr::null_mut();
        self.checksum = self.compute_checksum();
    }

    unsafe fn to_user_ptr(&self) -> *mut u8 {
        (self as *const HeapBlock as *mut u8).add(core::mem::size_of::<HeapBlock>())
    }

    unsafe fn from_user_ptr(ptr: *mut u8) -> *mut HeapBlock {
        ptr.sub(core::mem::size_of::<HeapBlock>()) as *mut HeapBlock
    }
}

fn align_size(size: usize) -> usize {
    (size + HEAP_ALIGN - 1) & !(HEAP_ALIGN - 1)
}

fn get_size_class(size: usize) -> Option<usize> {
    SIZE_CLASSES.iter().position(|&class_size| size <= class_size)
}

unsafe fn create_new_block(size: usize) -> *mut HeapBlock {
    let chunk_size = if size <= 1024 {
        if size < 4096 { 4096 } else { ((size * 4) + 4095) & !4095 }
    } else {
        size
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

    block
}

unsafe fn find_free_block(size: usize) -> *mut HeapBlock {
    let heap = HEAP.lock();
    let mut best: *mut HeapBlock = ptr::null_mut();
    let mut best_size = MAX_ALLOC_SIZE;

    let mut current = heap.head;
    while !current.is_null() {
        let block = &*current;
        if block.is_free != 0 && block.in_cache == 0 && block.size >= size {
            if block.size < best_size {
                best = current;
                best_size = block.size;
                if block.size == size {
                    break; // Perfect fit
                }
            }
        }
        current = block.next;
    }

    best
}

#[no_mangle]
pub unsafe extern "C" fn rust_kmalloc(size: usize) -> *mut u8 {
    if size == 0 || size > MAX_ALLOC_SIZE {
        return ptr::null_mut();
    }

    let aligned_size = align_size(size);

    // Try fast cache first for small allocations
    if let Some(size_class) = get_size_class(aligned_size) {
        let mut heap = HEAP.lock();
        let cache = &mut heap.fast_caches[size_class];
        if !cache.free_list.is_null() {
            let block = cache.free_list;
            cache.free_list = (*block).cache_next;
            cache.count -= 1;
            cache.hits += 1;

            (*block).cache_next = ptr::null_mut();
            (*block).in_cache = 0;
            (*block).is_free = 0;
            (*block).magic = HEAP_MAGIC_ALLOC;

            heap.total_allocated += (*block).size;
            if heap.total_allocated > heap.peak_allocated {
                heap.peak_allocated = heap.total_allocated;
            }

            return (*block).to_user_ptr();
        }
        cache.misses += 1;
    }

    // Find free block or create new one
    let block = find_free_block(aligned_size);
    let block = if block.is_null() {
        create_new_block(aligned_size)
    } else {
        (*block).is_free = 0;
        (*block).magic = HEAP_MAGIC_ALLOC;
        block
    };

    if block.is_null() {
        return ptr::null_mut();
    }

    let mut heap = HEAP.lock();
    heap.total_allocated += (*block).size;
    if heap.total_allocated > heap.peak_allocated {
        heap.peak_allocated = heap.total_allocated;
    }

    (*block).to_user_ptr()
}

#[no_mangle]
pub unsafe extern "C" fn rust_kfree(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }

    let block = HeapBlock::from_user_ptr(ptr);
    if (*block).magic != HEAP_MAGIC_ALLOC {
        PrintKernelError(b"[HEAP] Invalid magic in kfree\0".as_ptr());
        return;
    }

    (*block).is_free = 1;
    (*block).magic = HEAP_MAGIC_FREE;

    let mut heap = HEAP.lock();
    heap.total_allocated = heap.total_allocated.saturating_sub((*block).size);

    // Try to add to fast cache
    if let Some(size_class) = get_size_class((*block).size) {
        let cache = &mut heap.fast_caches[size_class];
        if cache.count < FAST_CACHE_SIZE as i32 {
            (*block).cache_next = cache.free_list;
            cache.free_list = block;
            cache.count += 1;
            (*block).in_cache = 1;
            return;
        }
    }

    // TODO: Implement coalescing for blocks not in cache
}

#[no_mangle]
pub unsafe extern "C" fn rust_krealloc(ptr: *mut u8, new_size: usize) -> *mut u8 {
    if ptr.is_null() {
        return rust_kmalloc(new_size);
    }

    if new_size == 0 {
        rust_kfree(ptr);
        return ptr::null_mut();
    }

    let block = HeapBlock::from_user_ptr(ptr);
    if (*block).magic != HEAP_MAGIC_ALLOC {
        return ptr::null_mut();
    }

    let old_size = (*block).size;
    if new_size <= old_size {
        return ptr; // No need to reallocate
    }

    let new_ptr = rust_kmalloc(new_size);
    if !new_ptr.is_null() {
        core::ptr::copy_nonoverlapping(ptr, new_ptr, old_size.min(new_size));
        rust_kfree(ptr);
    }

    new_ptr
}

#[no_mangle]
pub unsafe extern "C" fn rust_kcalloc(count: usize, size: usize) -> *mut u8 {
    let total_size = count.saturating_mul(size);
    let ptr = rust_kmalloc(total_size);
    if !ptr.is_null() {
        core::ptr::write_bytes(ptr, 0, total_size);
    }
    ptr
}