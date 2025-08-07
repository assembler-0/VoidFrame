pub mod vmm {

    // Re-export the CLayout for FFI
    pub use crate::CLayout;

    // Global allocator from lib.rs
    extern crate alloc;

    // Placeholder for the physical memory allocator (PMM)
    // In a real scenario, this would be an FFI call back to C or a Rust PMM.
    // For now, we'll assume a simple page allocator.
    extern "C" {
        fn AllocPage() -> *mut u8;
        fn FreePage(page: *mut u8);
        fn PrintKernel(s: *const u8);
        fn PrintKernelSuccess(s: *const u8);
        fn PrintKernelError(s: *const u8);
        fn PrintKernelHex(val: u64);
        fn PrintKernelInt(val: u64);
    }

    // Constants from VMem.h
    const PAGE_SIZE: usize = 4096;
    const PAGE_PRESENT: u64 = 0x001;
    const PAGE_WRITABLE: u64 = 0x002;
    const PT_INDEX_MASK: u64 = 0x1FF;
    const PML4_SHIFT: u64 = 39;
    const PDP_SHIFT: u64 = 30;
    const PD_SHIFT: u64 = 21;
    const PT_SHIFT: u64 = 12;
    const PT_ADDR_MASK: u64 = 0x000FFFFFFFFFF000;

    // A simple representation of a page table entry
    #[derive(Clone, Copy)]
    #[repr(transparent)]
    struct PageTableEntry(u64);

    impl PageTableEntry {
        fn is_present(&self) -> bool {
            (self.0 & PAGE_PRESENT) != 0
        }

        fn address(&self) -> u64 {
            self.0 & PT_ADDR_MASK
        }

        fn set_address(&mut self, addr: u64) {
            self.0 = (self.0 & !PT_ADDR_MASK) | (addr & PT_ADDR_MASK);
        }

        fn set_flags(&mut self, flags: u64) {
            self.0 = (self.0 & PT_ADDR_MASK) | flags;
        }
    }

    // Global PML4 pointer (physical address)
    static mut KERNEL_PML4_PHYS: u64 = 0;

    #[no_mangle]
    pub extern "C" fn RustVMemInit() -> i32 {
        unsafe {
            let pml4_phys = AllocPage() as u64;
            if pml4_phys == 0 {
                PrintKernelError(b"RustVMemInit: Failed to allocate PML4 table\n\0".as_ptr());
                return -1; // VMEM_ERROR_NOMEM
            }
            // Zero the PML4 table
            core::ptr::write_bytes(pml4_phys as *mut u8, 0, PAGE_SIZE);
            KERNEL_PML4_PHYS = pml4_phys;
            PrintKernelSuccess(b"[SYSTEM] Rust Virtual Memory Manager Initialized.\n\0".as_ptr());
            PrintKernel(b"  PML4 Physical Address: \0".as_ptr());
            PrintKernelHex(pml4_phys);
            PrintKernel(b"\n\0".as_ptr());
        }
        0 // VMEM_SUCCESS
    }

    // Helper to get or create a page table at a given level
    unsafe fn get_or_create_page_table(
        current_table_phys: u64,
        vaddr: u64,
        level: u64,
        create: bool,
    ) -> u64 {
        if current_table_phys == 0 {
            return 0;
        }

        let current_table_virt = current_table_phys as *mut PageTableEntry;

        let shift = 39 - (level * 9);
        let index = ((vaddr >> shift) & PT_INDEX_MASK) as usize;

        let entry = current_table_virt.add(index).read();

        if !entry.is_present() {
            if !create {
                return 0;
            }

            let new_table_phys = AllocPage() as u64;
            if new_table_phys == 0 {
                return 0;
            }
            core::ptr::write_bytes(new_table_phys as *mut u8, 0, PAGE_SIZE);

            let mut new_entry = PageTableEntry(0);
            new_entry.set_address(new_table_phys);
            new_entry.set_flags(PAGE_PRESENT | PAGE_WRITABLE);
            current_table_virt.add(index).write(new_entry);

            new_table_phys
        } else {
            entry.address()
        }
    }

    #[no_mangle]
    pub extern "C" fn RustVMemMap(vaddr: u64, paddr: u64, flags: u64) -> i32 {
        unsafe {
            if KERNEL_PML4_PHYS == 0 {
                PrintKernelError(b"RustVMemMap: VMM not initialized!\n\0".as_ptr());
                return -2; // VMEM_ERROR_INVALID_ADDR
            }

            let pdp_phys = get_or_create_page_table(KERNEL_PML4_PHYS, vaddr, 0, true);
            if pdp_phys == 0 {
                return -1; // VMEM_ERROR_NOMEM
            }

            let pd_phys = get_or_create_page_table(pdp_phys, vaddr, 1, true);
            if pd_phys == 0 {
                return -1; // VMEM_ERROR_NOMEM
            }

            let pt_phys = get_or_create_page_table(pd_phys, vaddr, 2, true);
            if pt_phys == 0 {
                return -1; // VMEM_ERROR_NOMEM
            }

            let pt_virt = pt_phys as *mut PageTableEntry;
            let pt_index = ((vaddr >> PT_SHIFT) & PT_INDEX_MASK) as usize;

            let mut entry = pt_virt.add(pt_index).read();

            if entry.is_present() {
                // Already mapped
                return -3; // VMEM_ERROR_ALREADY_MAPPED
            }

            entry.set_address(paddr);
            entry.set_flags(flags | PAGE_PRESENT);
            pt_virt.add(pt_index).write(entry);

            // TODO: Invalidate TLB entry
            // VMemFlushTLBSingle(vaddr);
        }
        0 // VMEM_SUCCESS
    }

    #[no_mangle]
    pub extern "C" fn RustVMemGetPML4PhysAddr() -> u64 {
        unsafe { KERNEL_PML4_PHYS }
    }
}
