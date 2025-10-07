use core::ptr;
use core::sync::atomic::{AtomicPtr, AtomicU64, AtomicUsize, Ordering};

const MAX_CPUS: usize = 64;
const PERCPU_SIZE_CLASSES: usize = 8;

// Node for the lock-free stack
#[repr(C)]
struct Node {
    ptr: *mut u8,
    next: *mut Node,
}

// A simple lock-free stack (Treiber stack)
#[repr(C)]
struct LockFreeStack {
    head: AtomicPtr<Node>,
    hits: AtomicU64,
    misses: AtomicU64,
}

impl LockFreeStack {
    const fn new() -> Self {
        Self {
            head: AtomicPtr::new(ptr::null_mut()),
            hits: AtomicU64::new(0),
            misses: AtomicU64::new(0),
        }
    }

    fn push(&self, ptr: *mut u8) {
         unsafe {
             let node_mem = backend_kmalloc(core::mem::size_of::<Node>());
             if node_mem.is_null() {
                 unsafe {
                     backend_kfree(ptr);
                 }
                 return;
             }
             let new_node = unsafe { &mut *(node_mem as *mut Node) };
             new_node.ptr = ptr;
             let mut head = self.head.load(Ordering::Relaxed);
             loop {
                 new_node.next = head;
                 match self.head.compare_exchange_weak(head, new_node, Ordering::Release, Ordering::Relaxed) {
                     Ok(_) => break,
                     Err(h) => head = h,
                 }
             }
         }
    }

    fn pop(&self) -> Option<*mut u8> {
        loop {
            let head = self.head.load(Ordering::Acquire);
            if head.is_null() {
                self.misses.fetch_add(1, Ordering::Relaxed);
                return None;
            }
            
            let next = unsafe { (*head).next };
            match self.head.compare_exchange_weak(head, next, Ordering::Release, Ordering::Relaxed) {
                Ok(_) => {
                    self.hits.fetch_add(1, Ordering::Relaxed);
                    let ptr = unsafe { (*head).ptr };
                    unsafe {
                        backend_kfree(head as *mut u8);
                    }
                    return Some(ptr);
                }
                Err(_) => continue,
            }
        }
    }
}

static PERCPU_CACHES: [[LockFreeStack; PERCPU_SIZE_CLASSES]; MAX_CPUS] = {
    const INIT_STACK: LockFreeStack = LockFreeStack::new();
    const INIT_CPU: [LockFreeStack; PERCPU_SIZE_CLASSES] = [INIT_STACK; PERCPU_SIZE_CLASSES];
    [INIT_CPU; MAX_CPUS]
};

static PERCPU_ENABLED: AtomicUsize = AtomicUsize::new(1);

extern "C" {
    fn lapic_get_id() -> usize;
}

// Import backend allocator functions
use crate::backend::{
    rust_kmalloc_backend as backend_kmalloc,
    rust_kfree_backend as backend_kfree,
    rust_krealloc_backend as backend_krealloc,
    rust_kcalloc_backend as backend_kcalloc,
    HeapBlock
};

#[inline]
fn get_percpu_size_class(size: usize) -> Option<usize> {
    match size {
        1..=32 => Some(0),
        33..=64 => Some(1), 
        65..=128 => Some(2),
        129..=256 => Some(3),
        257..=512 => Some(4),
        513..=1024 => Some(5),
        1025..=2048 => Some(6),
        2049..=4096 => Some(7),
        _ => None,
    }
}

// Main allocation function with per-CPU fast path
#[no_mangle]
pub unsafe extern "C" fn rust_kmalloc(size: usize) -> *mut u8 {
    if PERCPU_ENABLED.load(Ordering::Relaxed) == 0 {
        return backend_kmalloc(size);
    }

    if let Some(class) = get_percpu_size_class(size) {
        let cpu = lapic_get_id() % MAX_CPUS;
        let cache = &PERCPU_CACHES[cpu][class];
        
        if let Some(ptr) = cache.pop() {
            return ptr;
        }
    }
    
    backend_kmalloc(size)
}

#[no_mangle]
pub unsafe extern "C" fn rust_kfree(ptr: *mut u8) {
    if ptr.is_null() {
        return;
    }

    if PERCPU_ENABLED.load(Ordering::Relaxed) == 0 {
        backend_kfree(ptr);
        return;
    }

    // Get size from block header for caching decision
    let block = HeapBlock::from_user_ptr(ptr);
    let size = (*block).size;
    
    if let Some(class) = get_percpu_size_class(size) {
        let cpu = lapic_get_id() % MAX_CPUS;
        let cache = &PERCPU_CACHES[cpu][class];
        cache.push(ptr);
        return;
    }
    
    backend_kfree(ptr);
}

#[no_mangle]
pub unsafe extern "C" fn rust_krealloc(ptr: *mut u8, new_size: usize) -> *mut u8 {
    backend_krealloc(ptr, new_size)
}

#[no_mangle]
pub unsafe extern "C" fn rust_kcalloc(count: usize, size: usize) -> *mut u8 {
    backend_kcalloc(count, size)
}

// Control functions
#[no_mangle]
pub extern "C" fn rust_heap_enable_percpu() {
    PERCPU_ENABLED.store(1, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn rust_heap_disable_percpu() {
    PERCPU_ENABLED.store(0, Ordering::Relaxed);
}

#[no_mangle]
pub extern "C" fn rust_heap_flush_cpu(cpu: usize) {
    if cpu >= MAX_CPUS {
        return;
    }
    
    unsafe {
        for class in 0..PERCPU_SIZE_CLASSES {
            let cache = &PERCPU_CACHES[cpu][class];
            while let Some(ptr) = cache.pop() {
                backend_kfree(ptr);
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn rust_heap_get_percpu_stats(cpu: usize, hits: *mut u64, misses: *mut u64) {
    if cpu >= MAX_CPUS || hits.is_null() || misses.is_null() {
        return;
    }
    
    unsafe {
        let mut total_hits = 0;
        let mut total_misses = 0;
        
        for class in 0..PERCPU_SIZE_CLASSES {
            let cache = &PERCPU_CACHES[cpu][class];
            total_hits += cache.hits.load(Ordering::Relaxed);
            total_misses += cache.misses.load(Ordering::Relaxed);
        }
        
        *hits = total_hits;
        *misses = total_misses;
    }
}