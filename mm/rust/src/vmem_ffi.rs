// FFI declarations for VMem functions
extern "C" {
    pub fn VMemAlloc(size: u64) -> *mut u8;
    pub fn VMemFree(ptr: *mut u8, size: u64);
    pub fn VMemGetPhysAddr(vaddr: u64) -> u64;
    pub fn VMemMap(vaddr: u64, paddr: u64, flags: u64) -> i32;
    pub fn VMemUnmap(vaddr: u64, size: u64) -> i32;
}

// Console functions
extern "C" {
    pub fn PrintKernel(msg: *const u8);
    pub fn PrintKernelError(msg: *const u8);
    pub fn PrintKernelHex(value: u64);
    pub fn PrintKernelInt(value: u64);
}