#include "Memory.h"

#include "Console.h"
#include "MemOps.h"
#include "Multiboot2.h"
#include "Spinlock.h"
#include "VMem.h"

// Max 4GB memory for now (1M pages)
#define MAX_PAGES (4ULL * 1024 * 1024 * 1024 / PAGE_SIZE)
#define MAX_BITMAP_SIZE (MAX_PAGES / 8)
#define BITMAP_WORD_SIZE 64
#define BITMAP_WORDS (MAX_BITMAP_SIZE / 8)

extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];

static uint64_t allocation_count = 0;
static uint64_t free_count = 0;
static uint64_t huge_pages_allocated = 0;

// Use 64-bit words for faster bitmap operations
static uint64_t page_bitmap[BITMAP_WORDS];
uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static volatile int memory_lock = 0;
static uint64_t next_free_hint = 0x100000 / PAGE_SIZE;
static uint64_t low_memory_watermark = 0;
static uint64_t allocation_failures = 0;

// Fast bitmap operations using 64-bit words
static inline void MarkPageUsed(uint64_t page_idx) {
    if (page_idx >= total_pages) return;
    
    uint64_t word_idx = page_idx / 64;
    uint64_t bit_idx = page_idx % 64;
    uint64_t mask = 1ULL << bit_idx;
    
    if (!(page_bitmap[word_idx] & mask)) {
        page_bitmap[word_idx] |= mask;
        used_pages++;
    }
}

static inline void MarkPageFree(uint64_t page_idx) {
    if (page_idx >= total_pages) return;
    
    uint64_t word_idx = page_idx / 64;
    uint64_t bit_idx = page_idx % 64;
    uint64_t mask = 1ULL << bit_idx;
    
    if (page_bitmap[word_idx] & mask) {
        page_bitmap[word_idx] &= ~mask;
        used_pages--;
    }
}

int IsPageFree(uint64_t page_idx) {
    if (page_idx >= total_pages) return 0;
    
    uint64_t word_idx = page_idx / 64;
    uint64_t bit_idx = page_idx % 64;
    return !(page_bitmap[word_idx] & (1ULL << bit_idx));
}

// Find first free page in a 64-bit word
static inline int FindFirstFreeBit(uint64_t word) {
    if (word == ~0ULL) return -1; // All bits set
    return __builtin_ctzll(~word); // Count trailing zeros in inverted word
}

int MemoryInit(uint32_t multiboot_info_addr) {
    FastMemset(page_bitmap, 0, sizeof(page_bitmap));
    used_pages = 0;
    allocation_failures = 0;

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
                 const struct MultibootMmapEntry* entry = (struct MultibootMmapEntry*)((uint8_t*)mmap_tag + sizeof(struct MultibootTagMmap) + (i * mmap_tag->entry_size));

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
    const uint64_t kernel_start_addr = (uint64_t)_kernel_phys_start;
    const uint64_t kernel_end_addr = (uint64_t)_kernel_phys_end;

    const uint64_t kernel_start_page = kernel_start_addr / PAGE_SIZE;
    const uint64_t kernel_end_page = (kernel_end_addr + PAGE_SIZE - 1) / PAGE_SIZE;

    PrintKernel("[INFO] Reserving kernel memory from page ");
    PrintKernelInt(kernel_start_page);
    PrintKernel(" to ");
    PrintKernelInt(kernel_end_page);
    PrintKernel("\n");

    for (uint64_t i = kernel_start_page; i < kernel_end_page; i++) {
        MarkPageUsed(i);
    }

    // 3. (Optional but good) Reserve the memory used by the multiboot info
    // itself
    const uint64_t mb_info_start_page = multiboot_info_addr / PAGE_SIZE;
    const uint64_t mb_info_end_page = (multiboot_info_addr + total_multiboot_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint64_t i = mb_info_start_page; i < mb_info_end_page; i++) {
        MarkPageUsed(i);
    }
    PrintKernelSuccess("[SYSTEM] Physical memory manager initialized\n");
    return 0;
}


void* AllocPage(void) {
    irq_flags_t flags = SpinLockIrqSave(&memory_lock);
    
    // Check low memory condition
    if (used_pages > (total_pages * 9) / 10) { // 90% used
        if (low_memory_watermark == 0) {
            low_memory_watermark = used_pages;
            PrintKernelWarning("[MEMORY] Low memory warning: ");
            PrintKernelInt((total_pages - used_pages) * PAGE_SIZE / (1024 * 1024));
            PrintKernel("MB remaining\n");
        }
    }
    
    // Fast word-based search from hint
    uint64_t start_word = next_free_hint / 64;
    uint64_t total_words = (total_pages + 63) / 64;
    
    // Search from hint word onwards
    for (uint64_t word_idx = start_word; word_idx < total_words; word_idx++) {
        if (page_bitmap[word_idx] != ~0ULL) { // Not all bits set
            int bit_pos = FindFirstFreeBit(page_bitmap[word_idx]);
            if (bit_pos >= 0) {
                uint64_t page_idx = word_idx * 64 + bit_pos;
                if (page_idx >= total_pages) break;
                if (page_idx < 0x100000 / PAGE_SIZE) continue; // Skip low memory
                
                MarkPageUsed(page_idx);
                next_free_hint = page_idx + 1;
                void* page = (void*)(page_idx * PAGE_SIZE);
                SpinUnlockIrqRestore(&memory_lock, flags);
                return page;
            }
        }
    }
    
    // Search from beginning to hint
    uint64_t min_word = (0x100000 / PAGE_SIZE) / 64;
    for (uint64_t word_idx = min_word; word_idx < start_word; word_idx++) {
        if (page_bitmap[word_idx] != ~0ULL) {
            int bit_pos = FindFirstFreeBit(page_bitmap[word_idx]);
            if (bit_pos >= 0) {
                uint64_t page_idx = word_idx * 64 + bit_pos;
                if (page_idx < 0x100000 / PAGE_SIZE) continue;
                
                MarkPageUsed(page_idx);
                next_free_hint = page_idx + 1;
                void* page = (void*)(page_idx * PAGE_SIZE);
                SpinUnlockIrqRestore(&memory_lock, flags);
                return page;
            }
        }
    }
    
    allocation_failures++;
    SpinUnlockIrqRestore(&memory_lock, flags);
    return NULL; // Out of memory
}

void* AllocHugePages(uint64_t num_pages) {
    irq_flags_t flags = SpinLockIrqSave(&memory_lock);

    // Find contiguous 2MB-aligned region (512 pages)
    uint64_t pages_per_huge = HUGE_PAGE_SIZE / PAGE_SIZE;  // 512
    uint64_t total_needed = num_pages * pages_per_huge;

    // Search for aligned contiguous region
    for (uint64_t start = HUGE_PAGE_ALIGN_UP(0x100000) / PAGE_SIZE;
         start + total_needed <= total_pages;
         start += pages_per_huge) {

        // Check if all pages in this region are free
        int all_free = 1;
        for (uint64_t i = 0; i < total_needed; i++) {
            if (!IsPageFree(start + i)) {
                all_free = 0;
                break;
            }
        }

        if (all_free) {
            // Mark all pages as used
            for (uint64_t i = 0; i < total_needed; i++) {
                MarkPageUsed(start + i);
            }

            void* huge_page = (void*)(start * PAGE_SIZE);
            SpinUnlockIrqRestore(&memory_lock, flags);
            ++huge_pages_allocated;
            return huge_page;
        }
    }

    SpinUnlockIrqRestore(&memory_lock, flags);
    ++allocation_failures;
    return NULL;  // No contiguous region found
}


void FreePage(void* page) {
    if (!page) {
        PrintKernelError("[MEMORY] FreePage: NULL pointer\n");
        return;
    }

    uint64_t addr = (uint64_t)page;
    if (addr % PAGE_SIZE != 0) {
        PrintKernelError("[MEMORY] FreePage: Unaligned address ");
        PrintKernelHex(addr); PrintKernel("\n");
        return;
    }

    uint64_t page_idx = addr / PAGE_SIZE;
    if (page_idx >= total_pages) {
        PrintKernelError("[MEMORY] FreePage: Page index out of bounds: ");
        PrintKernelInt(page_idx); PrintKernel("\n");
        return;
    }

    irq_flags_t flags = SpinLockIrqSave(&memory_lock);
    
    // Check for double free
    if (IsPageFree(page_idx)) {
        SpinUnlockIrqRestore(&memory_lock, flags);
        PrintKernelError("[MEMORY] Double free of page ");
        PrintKernelHex(addr); PrintKernel("\n");
        return;
    }
    
    MarkPageFree(page_idx);
    
    // Update hint if this page is before current hint
    if (page_idx < next_free_hint) {
        next_free_hint = page_idx;
    }
    
    SpinUnlockIrqRestore(&memory_lock, flags);
}

uint64_t GetFreeMemory(void) {
    return (total_pages - used_pages) * PAGE_SIZE;
}

void GetDetailedMemoryStats(MemoryStats* stats) {
    irq_flags_t flags = SpinLockIrqSave(&memory_lock);

    stats->total_physical_bytes = total_pages * PAGE_SIZE;
    stats->used_physical_bytes = used_pages * PAGE_SIZE;
    stats->free_physical_bytes = (total_pages - used_pages) * PAGE_SIZE;
    stats->allocation_count = allocation_count;
    stats->free_count = free_count;
    stats->allocation_failures = allocation_failures;
    stats->huge_pages_allocated = huge_pages_allocated;

    // Calculate fragmentation score and largest free block
    uint64_t free_fragments = 0;
    uint64_t current_fragment = 0;
    uint64_t largest_fragment = 0;

    for (uint64_t i = 0x100000 / PAGE_SIZE; i < total_pages; i++) {
        if (IsPageFree(i)) {
            current_fragment++;
        } else {
            if (current_fragment > 0) {
                free_fragments++;
                if (current_fragment > largest_fragment) {
                    largest_fragment = current_fragment;
                }
                current_fragment = 0;
            }
        }
    }

    // Handle case where last pages are free
    if (current_fragment > 0) {
        free_fragments++;
        if (current_fragment > largest_fragment) {
            largest_fragment = current_fragment;
        }
    }

    stats->largest_free_block = largest_fragment * PAGE_SIZE;

    // Fragmentation score: more fragments = higher score
    uint64_t total_free_pages = total_pages - used_pages;
    if (total_free_pages > 0) {
        stats->fragmentation_score = (free_fragments * 100) / (total_free_pages / 10 + 1);
        if (stats->fragmentation_score > 100) stats->fragmentation_score = 100;
    } else {
        stats->fragmentation_score = 0;
    }

    SpinUnlockIrqRestore(&memory_lock, flags);
}

