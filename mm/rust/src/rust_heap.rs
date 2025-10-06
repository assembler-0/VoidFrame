use core::ptr;
use core::sync::atomic::{AtomicPtr, AtomicU64, AtomicUsize, Ordering};
use spin::Mutex;

const PERCPU_CACHE_SIZE: usize = 32;
const MAX_CPUS: usize = 64;
const PERCPU_SIZE_CLASSES: usize = 8;

// Per-CPU cache for hot allocation paths
#[repr(C)]
struct PercpuCache {
    objects: [AtomicPtr<u8>; PERCPU_CACHE_SIZE],
    count: AtomicUsize,
    hits: AtomicU64,
    misses: AtomicU64,
}

static PERCPU_CACHES: [Mutex<[PercpuCache; PERCPU_SIZE_CLASSES]>; MAX_CPUS] = {
    const INIT_CACHE: PercpuCache = PercpuCache {
        objects: [const { AtomicPtr::new(ptr::null_mut()) }; PERCPU_CACHE_SIZE],
        count: AtomicUsize::new(0),
        hits: AtomicU64::new(0),
        misses: AtomicU64::new(0),
    };
    const INIT_CPU: Mutex<[PercpuCache; PERCPU_SIZE_CLASSES]> = Mutex::new([INIT_CACHE; PERCPU_SIZE_CLASSES]);
    [INIT_CPU; MAX_CPUS]
};

static PERCPU_ENABLED: AtomicUsize = AtomicUsize::new(1);

extern "C" {
    fn lapic_get_id() -> usize;
}

// Import backend allocator functions
use crate::backend::{rust_kmalloc_backend as backend_kmalloc, rust_kfree_backend as backend_kfree, rust_krealloc_backend as backend_krealloc, rust_kcalloc_backend as backend_kcalloc, HeapBlock};

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
        let caches = PERCPU_CACHES[cpu].lock();
        let cache = &caches[class];
        
        let count = cache.count.load(Ordering::Relaxed);
        if count > 0 {
            let new_count = count - 1;
            if cache.count.compare_exchange(count, new_count, Ordering::Relaxed, Ordering::Relaxed).is_ok() {
                let ptr = cache.objects[new_count].load(Ordering::Relaxed);
                if !ptr.is_null() {
                    cache.hits.fetch_add(1, Ordering::Relaxed);
                    return ptr;
                }
            }
        }
        cache.misses.fetch_add(1, Ordering::Relaxed);
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
        let caches = PERCPU_CACHES[cpu].lock();
        let cache = &caches[class];
        
        let count = cache.count.load(Ordering::Relaxed);
        if count < PERCPU_CACHE_SIZE {
            if cache.count.compare_exchange(count, count + 1, Ordering::Relaxed, Ordering::Relaxed).is_ok() {
                cache.objects[count].store(ptr, Ordering::Relaxed);
                return;
            }
        }
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
        let caches = PERCPU_CACHES[cpu].lock();
        for class in 0..PERCPU_SIZE_CLASSES {
            let cache = &caches[class];
            let count = cache.count.swap(0, Ordering::Relaxed);
            for i in 0..count {
                let ptr = cache.objects[i].swap(ptr::null_mut(), Ordering::Relaxed);
                if !ptr.is_null() {
                    backend_kfree(ptr);
                }
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
        
        let caches = PERCPU_CACHES[cpu].lock();
        for class in 0..PERCPU_SIZE_CLASSES {
            let cache = &caches[class];
            total_hits += cache.hits.load(Ordering::Relaxed);
            total_misses += cache.misses.load(Ordering::Relaxed);
        }
        
        *hits = total_hits;
        *misses = total_misses;
    }
}