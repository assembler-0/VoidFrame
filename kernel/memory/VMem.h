/**
 * @file VMem.h
 * @brief Virtual Memory Manager Header - ENHANCED VERSION
 */

#ifndef VMEM_H
#define VMEM_H

#include "stdint.h"

// Page size constants
#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_MASK           0xFFF
#define IDENTITY_MAP_SIZE   (4ULL * 1024 * 1024 * 1024)  // Match bootstrap

// Page table entry flags
#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_USER           0x004
#define PAGE_WRITETHROUGH   0x008
#define PAGE_NOCACHE        0x010
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_LARGE          0x080
#define PAGE_GLOBAL         0x100
#define PAGE_NX             0x8000000000000000ULL

// Page table indices and masks
#define PT_INDEX_MASK       0x1FF
#define PML4_SHIFT          39
#define PDP_SHIFT           30
#define PD_SHIFT            21
#define PT_SHIFT            12
#define PT_ADDR_MASK        0x000FFFFFFFFFF000ULL
// Pages but BIGG
#define HUGE_PAGE_SIZE      (2 * 1024 * 1024)  // 2MB
#define HUGE_PAGE_SHIFT     21
#define HUGE_PAGE_MASK      (HUGE_PAGE_SIZE - 1)
#define HUGE_PAGE_ALIGN_UP(addr)   (((addr) + HUGE_PAGE_MASK) & ~HUGE_PAGE_MASK)
#define HUGE_PAGE_ALIGN_DOWN(addr) ((addr) & ~HUGE_PAGE_MASK)
#define IS_HUGE_PAGE_ALIGNED(addr) (((addr) & HUGE_PAGE_MASK) == 0)

// Virtual address space layout for Ring-0 Kernel
// Kernel space is the top 1TB of the higher half (PML4 entries 510-511).
// Heap space is the remaining ~126TB of the higher half.
#define KERNEL_VIRTUAL_OFFSET 0xFFFFFE0000000000ULL
#define KERNEL_VIRTUAL_BASE   KERNEL_VIRTUAL_OFFSET

// Heap space layout - Non-overlapping canonical addresses
#define VIRT_ADDR_SPACE_START 0xFFFF800000000000ULL
#define VIRT_ADDR_SPACE_END   0xFFFFFE0000000000ULL  // End of heap, start of kernel
#define KERNEL_SPACE_START    KERNEL_VIRTUAL_BASE    // Kernel starts here
#define KERNEL_SPACE_END      0xFFFFFFFFFFFFFFFFULL  // Kernel ends at top

// Address conversion macros
#define PHYS_TO_VIRT(paddr) ((void*)((uint64_t)(paddr) + KERNEL_VIRTUAL_OFFSET))
#define VIRT_TO_PHYS(vaddr) ((uint64_t)(vaddr) - KERNEL_VIRTUAL_OFFSET)

// Page alignment macros
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_MASK) & ~PAGE_MASK)
#define IS_PAGE_ALIGNED(addr) (((addr) & PAGE_MASK) == 0)
#define VALIDATE_PTR(ptr) do { if (!(ptr)) return NULL; } while(0)

#define VMEM_GUARD_PAGES    1  // Add guard pages around allocations

/**
 * @brief Represents a block of free virtual address space.
 */
typedef struct VMemFreeBlock {
    uint64_t base;
    uint64_t size;
    struct VMemFreeBlock* next;
} VMemFreeBlock;

/**
 * @brief Virtual address space structure
 */
typedef struct {
    uint64_t* pml4;          /**< Physical address of PML4 table */
    uint64_t next_vaddr;     /**< Next virtual address for bump allocation */
    uint64_t used_pages;     /**< Number of pages currently allocated */
    uint64_t total_mapped;   /**< Total bytes mapped in this space */
    VMemFreeBlock* free_list;/**< Head of the free list for VA space recycling */
} VirtAddrSpace;

/**
 * @brief Memory mapping flags
 */
typedef enum {
    VMEM_READ       = PAGE_PRESENT,
    VMEM_WRITE      = PAGE_PRESENT | PAGE_WRITABLE,
    VMEM_USER       = PAGE_PRESENT | PAGE_USER,
    VMEM_USER_WRITE = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER,
    VMEM_NOCACHE    = PAGE_PRESENT | PAGE_NOCACHE,
    VMEM_GLOBAL     = PAGE_PRESENT | PAGE_GLOBAL
} VMem_Flags;

/**
 * @brief Return codes for VMem operations
 */
typedef enum {
    VMEM_SUCCESS = 0,
    VMEM_ERROR_NOMEM = -1,
    VMEM_ERROR_INVALID_ADDR = -2,
    VMEM_ERROR_ALREADY_MAPPED = -3,
    VMEM_ERROR_NOT_MAPPED = -4,
    VMEM_ERROR_ALIGN = -5,
    VMEM_ERROR_NO_VSPACE = -6,
} VMem_Result;

// Core virtual memory functions
void VMemInit(void);
void* VMemAlloc(uint64_t size);
void VMemFree(void* vaddr, uint64_t size);
int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags);
int VMemUnmap(uint64_t vaddr, uint64_t size);
void PrintVMemStats(void);
void VMemMapKernel(uint64_t kernel_phys_start, uint64_t kernel_phys_end);

// Safer allocation with unmapped guard pages
void* VMemAllocWithGuards(uint64_t size);
void VMemFreeWithGuards(void* ptr, uint64_t size);

// VMem stack allocation
void* VMemAllocStack(uint64_t size);
void VMemFreeStack(void* stack_top, uint64_t size);

// MMIO-specific mapping functions (bypass RAM validation for hardware registers)
int VMemMapMMIO(uint64_t vaddr, uint64_t paddr, uint64_t size, uint64_t flags);
void VMemUnmapMMIO(uint64_t vaddr, uint64_t size);

// Page table management functions
uint64_t* VMemGetPageTable(uint64_t* pml4, uint64_t vaddr, int level, int create);
int VMemSetPageFlags(uint64_t vaddr, uint64_t flags);
uint64_t VMemGetPhysAddr(uint64_t vaddr);
int VMemIsPageMapped(uint64_t vaddr);

// Address space management
VirtAddrSpace* VMemCreateAddressSpace(void);
void VMemDestroyAddressSpace(VirtAddrSpace* space);
int VMemSwitchAddressSpace(VirtAddrSpace* space);

// Utility functions
void VMemFlushTLB(void);
void VMemFlushTLBSingle(uint64_t vaddr);
void VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped);
uint64_t VMemGetPML4PhysAddr(void);

// Debug functions
void VMemDumpPageTable(uint64_t vaddr);
void VMemValidatePageTable(uint64_t* pml4);
void VMemDumpFreeList(void);

#endif // VMEM_H