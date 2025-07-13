/**
 * @file VMem.c
 * @brief Fixed Implementation of the Virtual Memory Manager
 *
 * CRITICAL FIXES:
 * 1. Fixed PML4 address corruption in VMemInit
 * 2. Fixed physical address masking (PAGE_MASK -> PT_ADDR_MASK)
 * 3. Added proper error handling for PHYS_TO_VIRT conversion
 * 4. Fixed spinlock usage in VMemMap/VMemUnmap
 * 5. Added bounds checking for virtual addresses
 */

#include "VMem.h"
#include "Memory.h"
#include "MemOps.h"
#include "Kernel.h"
#include "Interrupts.h"
#include "Panic.h"

/**
 * @brief The kernel's virtual address space structure
 */
static VirtAddrSpace kernel_space;

/**
 * @brief Spinlock for virtual memory operations
 */
static volatile int vmem_lock = 0;

/**
 * @brief Physical address mask for page table entries
 * This is the critical fix - PAGE_MASK is for offset bits, not address bits
 */
#define PT_ADDR_MASK        0x000FFFFFFFFFF000ULL

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
 * @brief Validate that a physical address can be converted to virtual
 */
static inline int is_valid_phys_addr(uint64_t paddr) {
    // Basic sanity check - adjust limits based on your system
    return (paddr != 0 && paddr < 0x100000000ULL); // 4GB limit example
}

/**
 * @brief Initializes the kernel's virtual memory manager
 *
 * CRITICAL FIX: Don't overwrite the PML4 pointer after converting to virtual!
 */
void VMemInit(void) {
    // Allocate physical page for PML4
    void* pml4_phys = AllocPage();
    if (!pml4_phys) {
        Panic("VMemInit: Failed to allocate PML4 table");
        return;
    }

    // CRITICAL FIX: Store physical address BEFORE converting
    uint64_t pml4_phys_addr = (uint64_t)pml4_phys;

    // Convert to virtual address and zero it
    uint64_t* pml4_virt = (uint64_t*)PHYS_TO_VIRT(pml4_phys);
    FastZeroPage(pml4_virt);

    // Initialize kernel space tracking
    kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    kernel_space.used_pages = 0;
    kernel_space.total_mapped = 0;

    // CRITICAL FIX: Store physical address, not virtual!
    kernel_space.pml4 = (uint64_t*)pml4_phys_addr;

    PrintKernel("VMemInit: Initialized kernel virtual memory manager at phys");
    PrintKernel(pml4_phys);
    PrintKernel("\n");
}

/**
 * @brief Retrieves or creates a page table entry at a specific level
 *
 * CRITICAL FIX: Proper physical address masking and validation
 */
static uint64_t VMemGetPageTablePhys(uint64_t pml4_phys, uint64_t vaddr, int level, int create) {
    // Validate physical address
    if (!is_valid_phys_addr(pml4_phys)) {
        return 0;
    }

    // Convert physical address to virtual for access
    uint64_t* pml4_virt = (uint64_t*)PHYS_TO_VIRT(pml4_phys);

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
 * CRITICAL FIX: Proper spinlock usage (don't call VMemMap from inside lock!)
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
 * CRITICAL FIX: Don't call VMemMap while holding the lock (causes deadlock)
 */
void* VMemAlloc(uint64_t size) {
    if (size == 0) return NULL;

    size = PAGE_ALIGN_UP(size);

    vmem_spin_lock(&vmem_lock);

    uint64_t vaddr = kernel_space.next_vaddr;

    // Reserve the virtual address space
    kernel_space.next_vaddr += size;

    vmem_spin_unlock(&vmem_lock);

    // Now map pages without holding the lock
    uint64_t allocated_size = 0;
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        void* paddr = AllocPage();
        if (!paddr) {
            // Cleanup on failure
            if (allocated_size > 0) {
                VMemFree((void*)vaddr, allocated_size);
            }
            return NULL;
        }

        int result = VMemMap(vaddr + offset, (uint64_t)paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            // Free the physical page and cleanup
            FreePage(paddr);
            if (allocated_size > 0) {
                VMemFree((void*)vaddr, allocated_size);
            }
            return NULL;
        }

        allocated_size += PAGE_SIZE;
    }

    // Update tracking
    vmem_spin_lock(&vmem_lock);
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
 * CRITICAL FIX: Get physical addresses before unmapping
 */
void VMemFree(void* vaddr, uint64_t size) {
    if (!vaddr || size == 0) return;

    size = PAGE_ALIGN_UP(size);
    uint64_t current_vaddr = PAGE_ALIGN_DOWN((uint64_t)vaddr);

    // First pass: collect physical addresses
    uint64_t* phys_addrs = (uint64_t*)VMemAlloc(size / PAGE_SIZE * sizeof(uint64_t));
    if (!phys_addrs) return; // Can't allocate tracking array

    int page_count = 0;
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t page_vaddr = current_vaddr + offset;
        uint64_t paddr = VMemGetPhysAddr(page_vaddr);
        if (paddr) {
            phys_addrs[page_count++] = paddr;
        }
    }

    // Second pass: unmap
    VMemUnmap(current_vaddr, size);

    // Third pass: free physical pages
    for (int i = 0; i < page_count; i++) {
        FreePage((void*)phys_addrs[i]);
    }

    // Update tracking
    vmem_spin_lock(&vmem_lock);
    kernel_space.used_pages -= size / PAGE_SIZE;
    kernel_space.total_mapped -= size;
    vmem_spin_unlock(&vmem_lock);

    // Free the tracking array (recursive call, but small)
    VMemFree(phys_addrs, size / PAGE_SIZE * sizeof(uint64_t));
}

/**
 * @brief Gets the physical address for a virtual address
 *
 * CRITICAL FIX: Proper address masking and validation
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

    // CRITICAL FIX: Use proper address mask
    return (pt_virt[pt_index] & PT_ADDR_MASK) | (vaddr & PAGE_MASK);
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
    vmem_spin_lock(&vmem_lock);
    if (used_pages) *used_pages = kernel_space.used_pages;
    if (total_mapped) *total_mapped = kernel_space.total_mapped;
    vmem_spin_unlock(&vmem_lock);
}