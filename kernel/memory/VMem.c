//
// Created by Atheria on 7/15/25.
//
#include "VMem.h"
#include "Memory.h"
#include "MemOps.h"
#include "Kernel.h"
#include "Interrupts.h"
#include "Panic.h"
#include "Spinlock.h"
static VirtAddrSpace kernel_space;

static volatile int vmem_lock = 0;


extern uint64_t total_pages;

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
    // Allocate physical page for PML4
    void* pml4_phys = AllocPage();
    if (!pml4_phys) {
        PANIC("VMemInit: Failed to allocate PML4 table");
    }

    uint64_t pml4_phys_addr = (uint64_t)pml4_phys;

    if (pml4_phys_addr < (4 * 1024 * 1024)) {  // If it's in the first 4MB
        FastZeroPage((void*)pml4_phys_addr);   // Use identity mapping
    } else {
        PrintKernelWarning("[WARNING] PML4 allocated above 4MB, assuming zeroed\n");
    }

    // Initialize kernel space tracking
    kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    kernel_space.used_pages = 0;
    kernel_space.total_mapped = 0;

    // Store physical address
    kernel_space.pml4 = (uint64_t*)pml4_phys_addr;


    PrintKernelSuccess("[SYSTEM] Initialized kernel virtual memory manager at address: ");
    PrintKernelHex(pml4_phys_addr);
    PrintKernel("\n");
}

static uint64_t VMemGetPageTablePhys(uint64_t pml4_phys, uint64_t vaddr, int level, int create) {
    // Validate physical address
    if (!is_valid_phys_addr(pml4_phys)) {
        return 0;
    }

    // Convert physical address to virtual for access
    // NOTE: During early boot, we rely on identity mapping for page tables.
    uint64_t* pml4_virt = (uint64_t*)pml4_phys;

    // Calculate index based on level
    int shift = 39 - (level * 9);
    int index = (vaddr >> shift) & PT_INDEX_MASK;

    // Bounds check
    if (index >= 512) {
        return 0;
    }

    // Check if entry exists
    if (!(pml4_virt[index] & PAGE_PRESENT)) {
        if (!create) return 0;

        // Allocate new page table
        void* new_table_phys = AllocPage();
        if (!new_table_phys) return 0;

        // Validate new allocation
        if (!is_valid_phys_addr((uint64_t)new_table_phys)) {
            FreePage(new_table_phys);
            return 0;
        }

        // Zero the new table
        uint64_t* new_table_virt = (uint64_t*)PHYS_TO_VIRT(new_table_phys);
        FastZeroPage(new_table_virt);

        // Set the entry with physical address
        pml4_virt[index] = (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITABLE;

        return (uint64_t)new_table_phys;
    }

    // CRITICAL FIX: Use proper address mask, not PAGE_MASK
    return pml4_virt[index] & PT_ADDR_MASK;
}

/**
 * @brief Maps a physical address to a virtual address
 *
 * CRITICAL FIX: Proper error handling and lock management
 */
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

    // Set the final page table entry
    uint64_t* pt_virt = (uint64_t*)pt_phys;
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

/**
 * @brief Unmaps a virtual address
 *
 * CRITICAL FIX: Proper spinlock usage (don't call VMemMap from inside lock!)
 */


/**
 * @brief Allocates contiguous virtual memory
 *
 * CRITICAL FIX: Don't call VMemMap while holding the lock (causes deadlock)
 */
void* VMemAlloc(uint64_t size) {
    if (size == 0) return NULL;

    size = PAGE_ALIGN_UP(size);

    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

    uint64_t vaddr = kernel_space.next_vaddr;
    kernel_space.next_vaddr += size;

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

        // Get physical address first (this has its own locking internally)
        uint64_t paddr = VMemGetPhysAddr(current_vaddr);

        if (paddr == 0) continue; // Not mapped

        // Now acquire lock for modification
        irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
        // Navigate to the Page Table Entry (PTE)
        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 0);
        if (!pdp_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        // Get virtual address of the page table to modify it
        uint64_t* pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
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

uint64_t VMemGetPhysAddr(uint64_t vaddr) {
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 0);
    if (!pdp_phys) return 0;

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 0);
    if (!pd_phys) return 0;

    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 0);
    if (!pt_phys) return 0;

    uint64_t* pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    int pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    if (!(pt_virt[pt_index] & PAGE_PRESENT)) return 0;

    // CRITICAL FIX: Use proper address mask
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

/**
 * @brief Checks if a virtual address is mapped
 */
int VMemIsPageMapped(uint64_t vaddr) {
    return VMemGetPhysAddr(vaddr) != 0;
}

/**
 * @brief Flushes the entire TLB
 */
void VMemFlushTLB(void) {
    asm volatile(
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}

/**
 * @brief Flushes a single TLB entry
 */
void VMemFlushTLBSingle(uint64_t vaddr) {
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
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

uint64_t VMemGetPML4PhysAddr(void) {
    return (uint64_t)kernel_space.pml4;  // This is already physical
}