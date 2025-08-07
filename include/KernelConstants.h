#ifndef KERNEL_CONSTANTS_H
#define KERNEL_CONSTANTS_H

#include "stdint.h"

// Page size constants
#define PAGE_SIZE           4096
#define PAGE_SHIFT          12
#define PAGE_MASK           0xFFF

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

// CRITICAL FIX: Use consistent virtual address layout
#define KERNEL_VIRTUAL_OFFSET 0xFFFFFFFF80000000ULL
#define KERNEL_VIRTUAL_BASE   KERNEL_VIRTUAL_OFFSET  // Make them the same!

// Virtual address space layout
#define VIRT_ADDR_SPACE_START 0xFFFF800000000000ULL  // Start heap below kernel
#define VIRT_ADDR_SPACE_END   KERNEL_VIRTUAL_BASE    // End at kernel start
#define KERNEL_SPACE_START    KERNEL_VIRTUAL_BASE    // Kernel starts here
#define KERNEL_SPACE_END      0xFFFFFFFFFFFFFFFFULL  // Kernel ends at top

#endif // KERNEL_CONSTANTS_H
