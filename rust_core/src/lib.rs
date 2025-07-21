#![no_std]

use core::alloc::Layout;
use core::panic::PanicInfo;
use linked_list_allocator::LockedHeap;

pub mod vmm;

#[global_allocator]
static ALLOCATOR: LockedHeap = LockedHeap::empty();

#[repr(C)]
pub struct CLayout {
    size: usize,
    align: usize,
}

#[no_mangle]
pub extern "C" fn RustKernelHeapInit(heap_start: *mut u8, heap_size: usize) {
    unsafe {
        ALLOCATOR.lock().init(heap_start, heap_size);
    }
}

#[no_mangle]
pub extern "C" fn RustKernelMemoryAlloc(layout: CLayout) -> *mut u8 {
    let rust_layout = Layout::from_size_align(layout.size, layout.align).unwrap();
    match ALLOCATOR.lock().allocate_first_fit(rust_layout) {
        Ok(ptr) => ptr.as_ptr(),
        Err(_) => core::ptr::null_mut(),
    }
}

#[no_mangle]
pub extern "C" fn RustKernelFree(ptr: *mut u8, layout: CLayout) {
    unsafe {
        let rust_layout = Layout::from_size_align(layout.size, layout.align).unwrap();
        ALLOCATOR
            .lock()
            .deallocate(core::ptr::NonNull::new_unchecked(ptr), rust_layout);
    }
}

#[no_mangle]
pub extern "C" fn RustKernelCallLocation(size: usize, align: usize) -> *mut u8 {
    let layout = CLayout { size, align };
    let ptr = RustKernelMemoryAlloc(layout);
    if !ptr.is_null() {
        unsafe {
            core::ptr::write_bytes(ptr, 0, size);
        }
    }
    ptr
}

#[no_mangle]
pub extern "C" fn RustKernelRealLocation(
    ptr: *mut u8,
    old_layout: CLayout,
    new_size: usize,
) -> *mut u8 {
    let new_layout = CLayout { size: new_size, align: old_layout.align };
    let new_ptr = RustKernelMemoryAlloc(new_layout);
    if !new_ptr.is_null() {
        unsafe {
            core::ptr::copy_nonoverlapping(ptr, new_ptr, old_layout.size);
            RustKernelFree(ptr, old_layout);
        }
    }
    new_ptr
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}
