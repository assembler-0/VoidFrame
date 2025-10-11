#![no_std]
#![no_main]

mod backend;
mod config;
mod ffi;
mod rust_heap;

// Export heap types and stats
pub use backend::{HeapStats, HeapBlock};

// Export main API with per-CPU optimization
pub use rust_heap::{
    rust_kmalloc,
    rust_kfree,
    rust_krealloc,
    rust_kcalloc,
    rust_heap_enable_percpu,
    rust_heap_disable_percpu,
    rust_heap_flush_cpu,
    rust_heap_get_percpu_stats,
};

// Export heap management functions
pub use backend::{
    rust_heap_get_stats,
    rust_heap_validate,
};

// Export performance tuning functions
pub use config::{
    rust_heap_set_performance_mode,
    rust_heap_tune_parameters,
    PERF_MODE_FAST,
    PERF_MODE_BALANCED,
    PERF_MODE_SECURE,
};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    // Call existing kernel panic function
    extern "C" {
        fn Panic(msg: *const u8) -> !;
    }
    unsafe {
        Panic(b"Rust heap panic\0".as_ptr());
    }
}