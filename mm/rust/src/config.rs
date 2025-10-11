use core::sync::atomic::{AtomicUsize, Ordering};

// Runtime-tunable performance parameters
pub static VALIDATION_LEVEL: AtomicUsize = AtomicUsize::new(1); // 0=none, 1=basic, 2=full
pub static FAST_CACHE_SIZE: AtomicUsize = AtomicUsize::new(32);
pub static COALESCE_THRESHOLD: AtomicUsize = AtomicUsize::new(1000);
pub static SMALL_ALLOC_THRESHOLD: AtomicUsize = AtomicUsize::new(1024);

// Performance modes
pub const PERF_MODE_FAST: usize = 0;      // Minimal validation, aggressive caching
pub const PERF_MODE_BALANCED: usize = 1;  // Default mode
pub const PERF_MODE_SECURE: usize = 2;    // Full validation, security features

#[no_mangle]
pub extern "C" fn rust_heap_set_performance_mode(mode: usize) {
    match mode {
        PERF_MODE_FAST => {
            VALIDATION_LEVEL.store(0, Ordering::Relaxed);
            FAST_CACHE_SIZE.store(64, Ordering::Relaxed);
            COALESCE_THRESHOLD.store(2000, Ordering::Relaxed);
        },
        PERF_MODE_BALANCED => {
            VALIDATION_LEVEL.store(1, Ordering::Relaxed);
            FAST_CACHE_SIZE.store(32, Ordering::Relaxed);
            COALESCE_THRESHOLD.store(1000, Ordering::Relaxed);
        },
        PERF_MODE_SECURE => {
            VALIDATION_LEVEL.store(2, Ordering::Relaxed);
            FAST_CACHE_SIZE.store(16, Ordering::Relaxed);
            COALESCE_THRESHOLD.store(500, Ordering::Relaxed);
        },
        _ => {} // Invalid mode, ignore
    }
}

#[no_mangle]
pub extern "C" fn rust_heap_tune_parameters(
    validation_level: usize,
    cache_size: usize,
    coalesce_threshold: usize,
    small_threshold: usize
) {
    if validation_level <= 2 {
        VALIDATION_LEVEL.store(validation_level, Ordering::Relaxed);
    }
    if cache_size > 0 && cache_size <= 1024 {
        FAST_CACHE_SIZE.store(cache_size, Ordering::Relaxed);
    }
    if coalesce_threshold > 0 {
        COALESCE_THRESHOLD.store(coalesce_threshold, Ordering::Relaxed);
    }
    if small_threshold >= 32 && small_threshold <= 8192 {
        SMALL_ALLOC_THRESHOLD.store(small_threshold, Ordering::Relaxed);
    }
}