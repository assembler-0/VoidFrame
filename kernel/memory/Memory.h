#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"
#include "Multiboot2.h"

#define PAGE_SIZE 4096

int MemoryInit(uint32_t multiboot_info_addr);
void* AllocPage(void);
void FreePage(void* page);
uint64_t GetFreeMemory(void);
extern uint64_t total_pages;

#endif