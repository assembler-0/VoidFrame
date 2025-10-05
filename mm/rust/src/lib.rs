#![no_std]
#![no_main]



mod heap;
mod vmem_ffi;

pub use heap::*;

// Re-export C API functions
pub use heap::{
    rust_kmalloc as kmalloc,
    rust_kfree as kfree,
    rust_krealloc as krealloc,
    rust_kcalloc as kcalloc,
};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    // Call kernel panic - this should be provided by the kernel
    extern "C" {
        fn kernel_panic(msg: *const u8) -> !;
    }
    unsafe {
        kernel_panic(b"Rust heap panic\0".as_ptr());
    }
}