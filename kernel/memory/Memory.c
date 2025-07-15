#include "Memory.h"
#include "MemOps.h"
#include "Panic.h"
#include "Kernel.h"
#include "Multiboot2.h"
#include "Spinlock.h"

// Max 4GB memory for now (1M pages)
#define MAX_PAGES (4ULL * 1024 * 1024 * 1024 / PAGE_SIZE)
#define MAX_BITMAP_SIZE (MAX_PAGES / 8)
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];

static uint8_t page_bitmap[MAX_BITMAP_SIZE];
uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static volatile int memory_lock = 0;
static uint64_t memory_start = 0x100000; // Start after 1MB

// Helper to mark a page as used
static void MarkPageUsed(uint64_t page_idx) {
    if (page_idx >= total_pages) {
        return;
    }
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    if (!(page_bitmap[byte_idx] & (1 << bit_idx))) {
        page_bitmap[byte_idx] |= (1 << bit_idx);
        used_pages++;
    }
}

// Helper to mark a page as free
static void MarkPageFree(uint64_t page_idx) {
    if (page_idx >= total_pages) {
        return;
    }
    uint64_t byte_idx = page_idx / 8;
    uint8_t bit_idx = page_idx % 8;
    if (page_bitmap[byte_idx] & (1 << bit_idx)) {
        page_bitmap[byte_idx] &= ~(1 << bit_idx);
        used_pages--;
    }
}

int MemoryInit(uint32_t multiboot_info_addr) {
    FastMemset(page_bitmap, 0, MAX_BITMAP_SIZE);

    uint32_t total_multiboot_size = *(uint32_t*)multiboot_info_addr;
    struct MultibootTag* tag = (struct MultibootTag*)(multiboot_info_addr + 8);
    uint64_t max_physical_address = 0;

    // First pass: find the highest physical address to determine total_pages
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            struct MultibootTagMmap* mmap_tag = (struct MultibootTagMmap*)tag;
            for (uint32_t i = 0; i < (mmap_tag->size - sizeof(struct MultibootTagMmap)) / mmap_tag->entry_size; i++) {
                struct MultibootMmapEntry* entry = (struct MultibootMmapEntry*)((uint8_t*)mmap_tag + sizeof(struct MultibootTagMmap) + (i * mmap_tag->entry_size));
                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    uint64_t end_addr = entry->addr + entry->len;
                    if (end_addr > max_physical_address) {
                        max_physical_address = end_addr;
                    }
                }
            }
        }
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }

    total_pages = max_physical_address / PAGE_SIZE;
    if (total_pages > MAX_PAGES) {
        total_pages = MAX_PAGES;
        PrintKernelWarning("[WARN] Memory detected exceeds MAX_PAGES, capping at ");
        PrintKernelInt(MAX_PAGES * PAGE_SIZE / (1024 * 1024));
        PrintKernel("MB\n");
    }

    PrintKernel("[INFO] Total physical memory detected: ");
    PrintKernelInt(total_pages * PAGE_SIZE / (1024 * 1024));
    PrintKernel("MB ( ");
    PrintKernelInt(total_pages);
    PrintKernel(" pages)\n");

    tag = (struct MultibootTag*)(uintptr_t)(multiboot_info_addr + 8); // Reset tag pointer
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_MMAP) {
            struct MultibootTagMmap* mmap_tag = (struct MultibootTagMmap*)tag;
            for (uint32_t i = 0; i < (mmap_tag->size - sizeof(struct MultibootTagMmap)) / mmap_tag->entry_size; i++) {
                struct MultibootMmapEntry* entry = (struct MultibootMmapEntry*)((uint8_t*)mmap_tag + sizeof(struct MultibootTagMmap) + (i * mmap_tag->entry_size));

                uint64_t start_page = entry->addr / PAGE_SIZE;
                uint64_t end_page = (entry->addr + entry->len - 1) / PAGE_SIZE;

                if (end_page >= total_pages) {
                    end_page = total_pages - 1;
                }

                if (entry->type == MULTIBOOT2_MEMORY_AVAILABLE) {
                    // Mark available pages as free (they are already 0 from FastMemset)
                    // We will mark used pages later
                } else {
                    // Mark reserved/unavailable pages as used
                    for (uint64_t current_addr = entry->addr; current_addr < entry->addr + entry->len; current_addr += PAGE_SIZE) {
                        uint64_t page_idx = current_addr / PAGE_SIZE;
                        if (page_idx < total_pages) {
                            MarkPageUsed(page_idx);
                        }
                    }
                }
            }
        }
        tag = (struct MultibootTag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }

    PrintKernel("[INFO] Reserving first 1MB of physical memory.\n");
    for (uint64_t i = 0; i < 0x100000 / PAGE_SIZE; i++) {
        MarkPageUsed(i);
    }

    // 2. Reserve the physical memory used by the kernel itself.
    uint64_t kernel_start_addr = (uint64_t)_kernel_phys_start;
    uint64_t kernel_end_addr = (uint64_t)_kernel_phys_end;

    uint64_t kernel_start_page = kernel_start_addr / PAGE_SIZE;
    uint64_t kernel_end_page = (kernel_end_addr + PAGE_SIZE - 1) / PAGE_SIZE;

    PrintKernel("[INFO] Reserving kernel memory from page ");
    PrintKernelInt(kernel_start_page);
    PrintKernel(" to ");
    PrintKernelInt(kernel_end_page);
    PrintKernel("\n");

    for (uint64_t i = kernel_start_page; i < kernel_end_page; i++) {
        MarkPageUsed(i);
    }

    // 3. (Optional but good) Reserve the memory used by the multiboot info itself
    uint64_t mb_info_start_page = multiboot_info_addr / PAGE_SIZE;
    uint64_t mb_info_end_page = (multiboot_info_addr + total_multiboot_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = mb_info_start_page; i < mb_info_end_page; i++) {
        MarkPageUsed(i);
    }
    PrintKernelSuccess("[SYSTEM] Physical memory manager initialized");
    return 0;
}


void* AllocPage(void) {
    irq_flags_t flags = SpinLockIrqSave(&memory_lock);
    for (uint64_t i = 0; i < total_pages; i++) {
        uint64_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        if (!(page_bitmap[byte_idx] & (1 << bit_idx))) {
            MarkPageUsed(i);
            void* page = (void*)(i * PAGE_SIZE);
            SpinUnlockIrqRestore(&memory_lock, flags);
            return page;
        }
    }
    SpinUnlockIrqRestore(&memory_lock, flags);
    return NULL; // Out of memory
}

void FreePage(void* page) {
    irq_flags_t flags = SpinLockIrqSave(&memory_lock);
    if (!page) {
        SpinUnlockIrqRestore(&memory_lock, flags);
        Panic("FreePage: NULL pointer");
    }

    uint64_t addr = (uint64_t)page;
    if (addr % PAGE_SIZE != 0) {
        SpinUnlockIrqRestore(&memory_lock, flags);
        Panic("FreePage: Address not page aligned");
    }

    uint64_t page_idx = addr / PAGE_SIZE;
    if (page_idx >= total_pages) {
        SpinUnlockIrqRestore(&memory_lock, flags);
        Panic("FreePage: Page index out of bounds");
    }

    MarkPageFree(page_idx);
    SpinUnlockIrqRestore(&memory_lock, flags);
}

uint64_t GetFreeMemory(void) {
    return (total_pages - used_pages) * PAGE_SIZE;
}