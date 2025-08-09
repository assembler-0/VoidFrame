//
// Created by Atheria on 7/15/25.
//
#include "VMem.h"

#include "Console.h"
#include "MemOps.h"
#include "Memory.h"
#include "Panic.h"
#include "Spinlock.h"

static VirtAddrSpace kernel_space;
static volatile int vmem_lock = 0;
static uint64_t vmem_allocations = 0;
static uint64_t vmem_frees = 0;
static uint64_t tlb_flushes = 0;

extern uint64_t total_pages;
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];
extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _rodata_start[];
extern uint8_t _rodata_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t _bss_start[];
extern uint8_t _bss_end[];

static inline int is_valid_phys_addr(uint64_t paddr) {
    // Basic sanity check - adjust limits based on your system
    return (paddr != 0 && paddr < (total_pages * PAGE_SIZE));
}

void VMemInit(void) {
    // Get current PML4 from CR3 (set by bootstrap)
    uint64_t pml4_phys_addr;
    asm volatile("mov %%cr3, %0" : "=r"(pml4_phys_addr));
    pml4_phys_addr &= ~0xFFF; // Clear flags

    // Initialize kernel space tracking
    kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    kernel_space.used_pages = 0;
    kernel_space.total_mapped = IDENTITY_MAP_SIZE;
    kernel_space.pml4 = (uint64_t*)pml4_phys_addr;
    uint64_t kernel_size = (uint64_t)_kernel_phys_end - (uint64_t)_kernel_phys_start;
    kernel_space.total_mapped += PAGE_ALIGN_UP(kernel_size);
    // Now test identity mapping
    if (VMemGetPhysAddr(0x100000) != 0x100000) {
        PANIC("Bootstrap identity mapping failed - VALIDATION FAILED");
    }
    const uint64_t probe = IDENTITY_MAP_SIZE - PAGE_SIZE;
    if (VMemGetPhysAddr(probe) != probe) {
        PANIC("Bootstrap identity mapping failed at IDENTITY_MAP_SIZE boundary");
    }
    PrintKernelSuccess("[SYSTEM] VMem initialized using existing PML4: ");
    PrintKernelHex(pml4_phys_addr);
    PrintKernel("\n");
}

static uint64_t VMemGetPageTablePhys(uint64_t pml4_phys, uint64_t vaddr, int level, int create) {
    if (!is_valid_phys_addr(pml4_phys)) return 0;

    // Access the table via identity map when available, otherwise via higher-half mapping
    uint64_t* table_virt = (pml4_phys < IDENTITY_MAP_SIZE)
        ? (uint64_t*)pml4_phys
        : (uint64_t*)PHYS_TO_VIRT(pml4_phys);

    int shift = 39 - (level * 9);
    int index = (vaddr >> shift) & PT_INDEX_MASK;
    if (index >= 512) return 0;

    if (!(table_virt[index] & PAGE_PRESENT)) {
        if (!create) return 0;

        // Allocate page-table memory from identity-mapped low memory to ensure accessibility
        void* new_table_phys = NULL;
        for (int attempt = 0; attempt < 32; attempt++) {
            void* candidate = AllocPage();
            if (!candidate) break;
            if ((uint64_t)candidate < IDENTITY_MAP_SIZE) {
                new_table_phys = candidate;
                break;
            }
            // Not identity-mapped; return it to the pool and try again
            FreePage(candidate);
        }
        if (!new_table_phys) return 0;
        if (!is_valid_phys_addr((uint64_t)new_table_phys)) {
            FreePage(new_table_phys);
            return 0;
        }

        // Zero the new table using an address we can access
        if ((uint64_t)new_table_phys < IDENTITY_MAP_SIZE) {
            FastZeroPage(new_table_phys);
        } else {
            FastZeroPage(PHYS_TO_VIRT(new_table_phys));
        }

        table_virt[index] = (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITABLE;
        return (uint64_t)new_table_phys;
    }

    return table_virt[index] & PT_ADDR_MASK;
}

int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    // Validate alignment
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }

    // Validate addresses
    if (!is_valid_phys_addr(paddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    // Validate virtual address range
    if (vaddr < VIRT_ADDR_SPACE_START || vaddr >= VIRT_ADDR_SPACE_END) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    // Get PDP table
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Get PD table
    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Get PT table
    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 1);
    if (!pt_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Access PT through identity mapping if possible
    uint64_t* pt_virt;
    if (pt_phys < IDENTITY_MAP_SIZE) {
        pt_virt = (uint64_t*)pt_phys;
    } else {
        pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    }

    int pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    // Check if already mapped
    if (pt_virt[pt_index] & PAGE_PRESENT) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    // Set the mapping
    pt_virt[pt_index] = paddr | flags | PAGE_PRESENT;

    // Invalidate TLB
    VMemFlushTLBSingle(vaddr);

    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    return VMEM_SUCCESS;
}

// Map huge page in VMem.c
int VMemMapHuge(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!IS_HUGE_PAGE_ALIGNED(vaddr) || !IS_HUGE_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    // Get PDP table
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Get PD table
    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Access PD through identity mapping
    uint64_t* pd_virt = (pd_phys < IDENTITY_MAP_SIZE) ?
                        (uint64_t*)pd_phys : (uint64_t*)PHYS_TO_VIRT(pd_phys);

    int pd_index = (vaddr >> PD_SHIFT) & PT_INDEX_MASK;

    // Check if already mapped
    if (pd_virt[pd_index] & PAGE_PRESENT) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    // Set huge page mapping (PS bit = 1 for 2MB pages)
    pd_virt[pd_index] = paddr | flags | PAGE_PRESENT | PAGE_LARGE;

    VMemFlushTLBSingle(vaddr);
    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    return VMEM_SUCCESS;
}

void* VMemAlloc(uint64_t size) {
    if (size == 0) return NULL;

    size = PAGE_ALIGN_UP(size);

    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

    // Ensure heap starts in proper virtual address space
    if (kernel_space.next_vaddr < VIRT_ADDR_SPACE_START) {
        kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    }

    // Check if we have enough space
    if (kernel_space.next_vaddr + size > VIRT_ADDR_SPACE_END) {
        SpinUnlockIrqRestore(&vmem_lock, flags);
        return NULL; // Out of virtual address space
    }

    const uint64_t vaddr = kernel_space.next_vaddr;

    SpinUnlockIrqRestore(&vmem_lock, flags);

    // Map pages without holding the lock
    uint64_t allocated_size = 0;
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        void* paddr = AllocPage();
        if (!paddr) {
            if (allocated_size > 0) {
                VMemFree((void*)vaddr, allocated_size);
            }
            return NULL;
        }

        int result = VMemMap(vaddr + offset, (uint64_t)paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            FreePage(paddr);
            if (allocated_size > 0) {
                VMemFree((void*)vaddr, allocated_size);
            }
            PrintKernelError("[SYSTEM] VMemAlloc: VMemMap failed with code ");
            PrintKernelInt(result);
            PrintKernel("\n");
            return NULL;
        }

        allocated_size += PAGE_SIZE;
    }

    // Update tracking
    flags = SpinLockIrqSave(&vmem_lock);
    kernel_space.next_vaddr = vaddr + size;
    kernel_space.used_pages += size / PAGE_SIZE;
    kernel_space.total_mapped += size;
    SpinUnlockIrqRestore(&vmem_lock, flags);

    // Zero the allocated memory
    FastMemset((void*)vaddr, 0, size);
    return (void*)vaddr;
}

void VMemFree(void* vaddr, uint64_t size) {
    if (!vaddr || size == 0) return;

    size = PAGE_ALIGN_UP(size);
    uint64_t start_vaddr = PAGE_ALIGN_DOWN((uint64_t)vaddr);
    uint64_t num_pages = size / PAGE_SIZE;

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = start_vaddr + (i * PAGE_SIZE);
        // acquire lock for modification
        const irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

        // Get physical address first (this has its own locking internally)
        uint64_t paddr = VMemGetPhysAddr(current_vaddr);
        if (paddr == 0) {
            SpinUnlockIrqRestore(&vmem_lock, flags);
            continue;
        }
        // Navigate to the Page Table Entry (PTE)
        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 0);
        if (!pdp_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        // Access PT through identity mapping if possible
        uint64_t* pt_virt;
        if (pt_phys < IDENTITY_MAP_SIZE) {
            pt_virt = (uint64_t*)pt_phys;
        } else {
            pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
        }
        int pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        // Check if it's actually mapped before proceeding
        if (pt_virt[pt_index] & PAGE_PRESENT) {
            // Clear the entry (unmap)
            pt_virt[pt_index] = 0;

            // Flush the TLB for this specific address
            VMemFlushTLBSingle(current_vaddr);

            // Update stats
            kernel_space.used_pages--;
            kernel_space.total_mapped -= PAGE_SIZE;
        }

        SpinUnlockIrqRestore(&vmem_lock, flags);

        // Free physical page outside the lock
        FreePage((void*)paddr);
    }
}

void* VMemAllocWithGuards(uint64_t size) {
    if (size == 0) return NULL;

    // Add space for guard pages (one before, one after)
    uint64_t total_size = size + (2 * PAGE_SIZE);
    void* base = VMemAlloc(total_size);
    if (!base) return NULL;

    uint64_t base_addr = (uint64_t)base;

    // Map guard pages with no permissions (just present bit)
    VMemMap(base_addr, VMemGetPhysAddr(base_addr), PAGE_PRESENT);  // First guard
    VMemMap(base_addr + PAGE_SIZE + size,
            VMemGetPhysAddr(base_addr + PAGE_SIZE + size), PAGE_PRESENT);  // Last guard

    // Fill guard pages with pattern
    *(uint64_t*)base_addr = GUARD_PAGE_PATTERN;
    *(uint64_t*)(base_addr + PAGE_SIZE + size) = GUARD_PAGE_PATTERN;

    return (void*)(base_addr + PAGE_SIZE);  // Return user-accessible area
}

void VMemFreeWithGuards(void* ptr, uint64_t size) {
    if (!ptr) return;

    uint64_t base_addr = (uint64_t)ptr - PAGE_SIZE;

    // Check guard pages for corruption
    if (*(uint64_t*)base_addr != GUARD_PAGE_PATTERN) {
        PrintKernelError("[VMEM] Guard page corruption detected at start!\n");
    }
    if (*(uint64_t*)(base_addr + PAGE_SIZE + size) != GUARD_PAGE_PATTERN) {
        PrintKernelError("[VMEM] Guard page corruption detected at end!\n");
    }

    VMemFree((void*)base_addr, size + (2 * PAGE_SIZE));
}

uint64_t VMemGetPhysAddr(uint64_t vaddr) {
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 0);
    if (!pdp_phys) return 0;

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 0);
    if (!pd_phys) return 0;

    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 0);
    if (!pt_phys) return 0;

    // Access PT through identity mapping if possible
    uint64_t* pt_virt;
    if (pt_phys < IDENTITY_MAP_SIZE) {
        pt_virt = (uint64_t*)pt_phys;
    } else {
        pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    }
    int pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    if (!(pt_virt[pt_index] & PAGE_PRESENT)) return 0;

    return (pt_virt[pt_index] & PT_ADDR_MASK) | (vaddr & PAGE_MASK);
}

void VMemMapKernel(uint64_t kernel_phys_start, uint64_t kernel_phys_end) {
    (void)kernel_phys_start; // Unused, but kept for signature compatibility
    (void)kernel_phys_end;   // Unused, but kept for signature compatibility

    PrintKernelSuccess("[SYSTEM] VMem: Mapping kernel sections...\n");

    // Map .text section (read-only)
    uint64_t text_start = PAGE_ALIGN_DOWN((uint64_t)_text_start);
    uint64_t text_end = PAGE_ALIGN_UP((uint64_t)_text_end);
    for (uint64_t paddr = text_start; paddr < text_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_PRESENT);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .text page!", result);
        }
    }
    PrintKernel("  .text mapped (RO): 0x"); PrintKernelHex(text_start); PrintKernel(" - 0x"); PrintKernelHex(text_end); PrintKernel("\n");

    // Map .rodata section (read-only)
    uint64_t rodata_start = PAGE_ALIGN_DOWN((uint64_t)_rodata_start);
    uint64_t rodata_end = PAGE_ALIGN_UP((uint64_t)_rodata_end);
    for (uint64_t paddr = rodata_start; paddr < rodata_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_PRESENT);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .rodata page!", result);
        }
    }
    PrintKernel("  .rodata mapped (RO): 0x"); PrintKernelHex(rodata_start); PrintKernel(" - 0x"); PrintKernelHex(rodata_end); PrintKernel("\n");

    // Map .data section (read-write)
    uint64_t data_start = PAGE_ALIGN_DOWN((uint64_t)_data_start);
    uint64_t data_end = PAGE_ALIGN_UP((uint64_t)_data_end);
    for (uint64_t paddr = data_start; paddr < data_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .data page!", result);
        }
    }
    PrintKernel("  .data mapped (RW): 0x"); PrintKernelHex(data_start); PrintKernel(" - 0x"); PrintKernelHex(data_end); PrintKernel("\n");

    // Map .bss section (read-write)
    uint64_t bss_start = PAGE_ALIGN_DOWN((uint64_t)_bss_start);
    uint64_t bss_end = PAGE_ALIGN_UP((uint64_t)_bss_end);
    for (uint64_t paddr = bss_start; paddr < bss_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .bss page!", result);
        }
    }
    PrintKernel("  .bss mapped (RW): 0x"); PrintKernelHex(bss_start); PrintKernel(" - 0x"); PrintKernelHex(bss_end); PrintKernel("\n");

    PrintKernelSuccess("[SYSTEM] VMem: Kernel section mapping complete.\n");
}

int VMemIsPageMapped(uint64_t vaddr) {
    return VMemGetPhysAddr(vaddr) != 0;
}

void VMemFlushTLB(void) {
    asm volatile(
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}

void VMemFlushTLBSingle(uint64_t vaddr) {
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    tlb_flushes++;
}

int VMemUnmap(uint64_t vaddr, uint64_t size) {
    if (size == 0) return VMEM_SUCCESS;
    
    size = PAGE_ALIGN_UP(size);
    uint64_t num_pages = size / PAGE_SIZE;
    
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);
        
        irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
        
        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 0);
        if (!pdp_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t* pt_virt;
        if (pt_phys < IDENTITY_MAP_SIZE) {
            pt_virt = (uint64_t*)pt_phys;
        } else {
            pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
        }
        int pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            pt_virt[pt_index] = 0;
            kernel_space.used_pages--;
            kernel_space.total_mapped -= PAGE_SIZE;
        }
        
        SpinUnlockIrqRestore(&vmem_lock, flags);
        VMemFlushTLBSingle(current_vaddr);
    }
    
    return VMEM_SUCCESS;
}

/**
 * @brief Gets virtual memory statistics
 */
void VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped) {
    SpinLock(&vmem_lock);
    if (used_pages) *used_pages = kernel_space.used_pages;
    if (total_mapped) *total_mapped = kernel_space.total_mapped;
    SpinUnlock(&vmem_lock);
}

void PrintVMemStats(void) {
    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
    uint64_t used = kernel_space.used_pages;
    uint64_t mapped = kernel_space.total_mapped;
    uint64_t allocs = vmem_allocations;
    uint64_t frees = vmem_frees;
    uint64_t flushes = tlb_flushes;
    SpinUnlockIrqRestore(&vmem_lock, flags);
    
    PrintKernel("[VMEM] Stats:\n");
    PrintKernel("  Used pages: "); PrintKernelInt(used); PrintKernel("\n");
    PrintKernel("  Mapped: "); PrintKernelInt(mapped / (1024 * 1024)); PrintKernel("MB\n");
    PrintKernel("  Allocs: "); PrintKernelInt(allocs); PrintKernel(", Frees: "); PrintKernelInt(frees); PrintKernel("\n");
    PrintKernel("  TLB flushes: "); PrintKernelInt(flushes); PrintKernel("\n");
}

uint64_t VMemGetPML4PhysAddr(void) {
    return (uint64_t)kernel_space.pml4;  // This is already physical
}