#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"

typedef struct MemoryStats {
    uint64_t total_physical_bytes;
    uint64_t used_physical_bytes;
    uint64_t free_physical_bytes;
    uint64_t allocation_count;
    uint64_t free_count;
    uint64_t allocation_failures;
    uint64_t huge_pages_allocated;
    uint64_t fragmentation_score;  // 0-100, higher = more fragmented
    uint64_t largest_free_block;   // Size of largest contiguous free block
} MemoryStats;


#define PAGE_SIZE 4096
#define HUGE_PAGE_SIZE (2 * 1024 * 1024) // ADDED: For clarity in calculations

extern uint64_t total_pages;

int MemoryInit(uint32_t multiboot_info_addr);

void* AllocPage(void);
void FreePage(void* page);
void* AllocHugePages(uint64_t num_pages);
void FreeHugePages(void* pages, uint64_t num_pages); // ADDED: Matching declaration for the new function

// Misc.
void GetDetailedMemoryStats(MemoryStats* stats);
int IsPageFree(uint64_t page_idx);
uint64_t GetFreeMemory(void);

#endif