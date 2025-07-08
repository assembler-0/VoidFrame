#include "Memory.h"
#include "MemOps.h"
#include "Cpu.h"
#include "Kernel.h"
#include "Panic.h"
static uint8_t page_bitmap[BITMAP_SIZE / 8];
static uint64_t total_pages = 0;
static uint64_t used_pages = 0;
static uint64_t memory_start = 0x100000; // Start after 1MB

void MemoryInit(void) {
    total_pages = BITMAP_SIZE;
    used_pages = 0;
    
    // Clear bitmap (all pages free) - use simple loop for now
    for (int i = 0; i < BITMAP_SIZE / 8; i++) {
        page_bitmap[i] = 0;
    }
    
    // Mark first 256 pages as used (kernel space)
    for (int i = 0; i < 256; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        page_bitmap[byte_idx] |= (1 << bit_idx);
        used_pages++;
    }
}

void* AllocPage(void) {
    for (int i = 256; i < total_pages; i++) {
        int byte_idx = i / 8;
        int bit_idx = i % 8;
        if (!(page_bitmap[byte_idx] & (1 << bit_idx))) {
            page_bitmap[byte_idx] |= (1 << bit_idx);
            used_pages++;
            void* page = (void*)(memory_start + i * PAGE_SIZE);
            return page;
        }
    }
    return 0; // Out of memory
}

void FreePage(void* page) {
    if (!page) {
        Panic("FreePage: NULL pointer");
    }
    
    uint64_t addr = (uint64_t)page;
    if (addr < memory_start) {
        Panic("FreePage: Address below memory start");
    }
    
    int page_idx = (addr - memory_start) / PAGE_SIZE;
    if (page_idx >= total_pages) {
        Panic("FreePage: Page index out of bounds");
    }
    
    int byte_idx = page_idx / 8;
    int bit_idx = page_idx % 8;
    
    if (page_bitmap[byte_idx] & (1 << bit_idx)) {
        page_bitmap[byte_idx] &= ~(1 << bit_idx);
        used_pages--;
    }
}

uint64_t GetFreeMemory(void) {
    return (total_pages - used_pages) * PAGE_SIZE;
}