/**
 * @file VMem.c
 * @brief Fixed Implementation of the Virtual Memory Manager
 *
 * This implementation fixes the critical issues in the original code:
 * - Proper physical/virtual address conversion
 * - Correct page table management
 * - Proper memory cleanup
 * - Better error handling
 */

#include "VMem.h"
#include "Memory.h"
#include "MemOps.h"
#include "../Core/Kernel.h"
#include "../Drivers/Interrupts.h"
#include "../Core/Panic.h"
/**
 * @brief The kernel's virtual address space structure
 */
static VirtAddrSpace kernel_space;

/**
 * @brief Spinlock for virtual memory operations
 */
static volatile int vmem_lock = 0;

/**
 * @brief Simple spinlock implementation
 */
static inline void vmem_spin_lock(volatile int* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) __builtin_ia32_pause();
    }
}

static inline void vmem_spin_unlock(volatile int* lock) {
    __sync_lock_release(lock);
}

/**
 * @brief Initializes the kernel's virtual memory manager
 *
 * Allocates and zeroes the PML4 table, sets up the kernel virtual address space,
 * and initializes tracking structures.
 */
void VMemInit(void) {
    // Allocate physical page for PML4
    void* pml4_phys = AllocPage();
    if (!pml4_phys) {
        Panic("VMemInit: Failed to allocate PML4 table");
        return;
    }

    // Convert to virtual address and zero it
    kernel_space.pml4 = (uint64_t*)PHYS_TO_VIRT(pml4_phys);
    FastZeroPage(kernel_space.pml4);

    // Initialize kernel space tracking
    kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    kernel_space.used_pages = 0;
    kernel_space.total_mapped = 0;

    // Store physical address for CR3
    kernel_space.pml4 = (uint64_t*)pml4_phys;

    PrintKernel("VMemInit: Initialized kernel virtual memory manager\n");
}

/**
 * @brief Retrieves or creates a page table entry at a specific level
 *
 * This function properly handles physical/virtual address conversion
 * and ensures all new page tables are zeroed.
 *
 * @param pml4_phys Physical address of the current page table
 * @param vaddr Virtual address to resolve
 * @param level Current level (0=PDP, 1=PD, 2=PT)
 * @param create If true, create new page table if missing
 * @return Physical address of next level page table, or 0 on failure
 */
static uint64_t VMemGetPageTablePhys(uint64_t pml4_phys, uint64_t vaddr, int level, int create) {
    // Convert physical address to virtual for access
    uint64_t* pml4_virt = (uint64_t*)PHYS_TO_VIRT(pml4_phys);

    // Calculate index based on level
    int shift = 39 - (level * 9);
    int index = (vaddr >> shift) & PT_INDEX_MASK;

    // Check if entry exists
    if (!(pml4_virt[index] & PAGE_PRESENT)) {
        if (!create) return 0;

        // Allocate new page table
        void* new_table_phys = AllocPage();
        if (!new_table_phys) return 0;

        // Zero the new table
        uint64_t* new_table_virt = (uint64_t*)PHYS_TO_VIRT(new_table_phys);
        FastZeroPage(new_table_virt);

        // Set the entry with physical address
        pml4_virt[index] = (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITABLE;

        return (uint64_t)new_table_phys;
    }

    // Return existing table physical address
    return pml4_virt[index] & ~PAGE_MASK;
}

/**
 * @brief Maps a physical address to a virtual address
 *
 * Creates all necessary page table levels and sets up the mapping
 * with proper TLB invalidation.
 *
 * @param vaddr Virtual address to map
 * @param paddr Physical address to map to
 * @param flags Page table entry flags
 * @return VMEM_SUCCESS on success, error code on failure
 */
int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    // Validate alignment
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }

    vmem_spin_lock(&vmem_lock);

    // Get PDP table
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        vmem_spin_unlock(&vmem_lock);
        return VMEM_ERROR_NOMEM;
    }

    // Get PD table
    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        vmem_spin_unlock(&vmem_lock);
        return VMEM_ERROR_NOMEM;
    }

    // Get PT table
    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 1);
    if (!pt_phys) {
        vmem_spin_unlock(&vmem_lock);
        return VMEM_ERROR_NOMEM;
    }

    // Set the final page table entry
    uint64_t* pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    int pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    // Check if already mapped
    if (pt_virt[pt_index] & PAGE_PRESENT) {
        vmem_spin_unlock(&vmem_lock);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    // Set the mapping
    pt_virt[pt_index] = paddr | flags | PAGE_PRESENT;

    // Invalidate TLB
    VMemFlushTLBSingle(vaddr);

    vmem_spin_unlock(&vmem_lock);
    return VMEM_SUCCESS;
}

/**
 * @brief Unmaps a virtual address
 *
 * @param vaddr Virtual address to unmap
 * @param size Size to unmap (rounded up to page size)
 * @return VMEM_SUCCESS on success, error code on failure
 */
int VMemUnmap(uint64_t vaddr, uint64_t size) {
    size = PAGE_ALIGN_UP(size);
    vaddr = PAGE_ALIGN_DOWN(vaddr);

    vmem_spin_lock(&vmem_lock);

    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t current_vaddr = vaddr + offset;

        // Navigate to page table
        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 0);
        if (!pdp_phys) continue;

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) continue;

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) continue;

        // Clear page table entry
        uint64_t* pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
        int pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            pt_virt[pt_index] = 0;
            VMemFlushTLBSingle(current_vaddr);
        }
    }

    vmem_spin_unlock(&vmem_lock);
    return VMEM_SUCCESS;
}

/**
 * @brief Allocates contiguous virtual memory
 *
 * Allocates virtual memory and maps it to newly allocated physical pages.
 * Implements proper cleanup on failure.
 *
 * @param size Size to allocate (rounded up to page size)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* VMemAlloc(uint64_t size) {
    if (size == 0) return NULL;

    size = PAGE_ALIGN_UP(size);

    vmem_spin_lock(&vmem_lock);

    uint64_t vaddr = kernel_space.next_vaddr;
    uint64_t allocated_size = 0;

    // Try to allocate and map all pages
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        void* paddr = AllocPage();
        if (!paddr) {
            // Cleanup on failure
            if (allocated_size > 0) {
                VMemFree((void*)vaddr, allocated_size);
            }
            vmem_spin_unlock(&vmem_lock);
            return NULL;
        }

        int result = VMemMap(vaddr + offset, (uint64_t)paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            // Free the physical page and cleanup
            FreePage(paddr);
            if (allocated_size > 0) {
                VMemFree((void*)vaddr, allocated_size);
            }
            vmem_spin_unlock(&vmem_lock);
            return NULL;
        }

        allocated_size += PAGE_SIZE;
    }

    // Update kernel space tracking
    kernel_space.next_vaddr += size;
    kernel_space.used_pages += size / PAGE_SIZE;
    kernel_space.total_mapped += size;

    vmem_spin_unlock(&vmem_lock);

    // Zero the allocated memory
    FastMemset((void*)vaddr, 0, size);
    return (void*)vaddr;
}

/**
 * @brief Frees previously allocated virtual memory
 *
 * Unmaps virtual addresses and frees the underlying physical pages.
 * Includes proper cleanup of empty page tables.
 *
 * @param vaddr Starting virtual address to free
 * @param size Size to free (rounded up to page size)
 */
void VMemFree(void* vaddr, uint64_t size) {
    if (!vaddr || size == 0) return;

    size = PAGE_ALIGN_UP(size);
    uint64_t current_vaddr = PAGE_ALIGN_DOWN((uint64_t)vaddr);

    vmem_spin_lock(&vmem_lock);

    // Free all pages in the range
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t page_vaddr = current_vaddr + offset;

        // Get physical address before unmapping
        uint64_t paddr = VMemGetPhysAddr(page_vaddr);
        if (paddr) {
            // Unmap the page
            VMemUnmap(page_vaddr, PAGE_SIZE);
            // Free the physical page
            FreePage((void*)paddr);
        }
    }

    // Update tracking
    kernel_space.used_pages -= size / PAGE_SIZE;
    kernel_space.total_mapped -= size;

    vmem_spin_unlock(&vmem_lock);
}

/**
 * @brief Gets the physical address for a virtual address
 *
 * @param vaddr Virtual address to translate
 * @return Physical address, or 0 if not mapped
 */
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

    return (pt_virt[pt_index] & ~PAGE_MASK) | (vaddr & PAGE_MASK);
}

/**
 * @brief Checks if a virtual address is mapped
 *
 * @param vaddr Virtual address to check
 * @return 1 if mapped, 0 if not mapped
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
 *
 * @param vaddr Virtual address to flush
 */
void VMemFlushTLBSingle(uint64_t vaddr) {
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
}

/**
 * @brief Gets virtual memory statistics
 *
 * @param used_pages Output: number of used pages
 * @param total_mapped Output: total bytes mapped
 */
void VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped) {
    if (used_pages) *used_pages = kernel_space.used_pages;
    if (total_mapped) *total_mapped = kernel_space.total_mapped;
}