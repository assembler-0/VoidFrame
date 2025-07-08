#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"

#define PAGE_SIZE 4096
#define BITMAP_SIZE 32768  // Support up to 128MB (32768 * 4KB)

void MemoryInit(void);
void* AllocPage(void);
void FreePage(void* page);
uint64_t GetFreeMemory(void);

#endif