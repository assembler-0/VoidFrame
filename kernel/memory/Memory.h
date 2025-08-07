#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"
#include "Multiboot2.h"

#define PAGE_SIZE 4096

/**
 * @brief Initialize the physical memory manager
 * @param multiboot_info_addr Address of multiboot2 info structure
 * @return 0 on success, negative on error
 */
int MemoryInit(uint32_t multiboot_info_addr);

/**
 * @brief Allocate a single physical page
 * @return Physical address of allocated page, or NULL if out of memory
 */
void* AllocPage(void);

/**
 * @brief Free a physical page
 * @param page Physical address of page to free (must be page-aligned)
 */
void FreePage(void* page);

/**
 * @brief Get amount of free physical memory
 * @return Number of free bytes
 */
uint64_t GetFreeMemory(void);

/**
 * @brief Print detailed memory statistics
 */
void PrintMemoryStats(void);

extern uint64_t total_pages;

#endif