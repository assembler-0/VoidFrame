#ifndef PHYSICAL_MEMORY_MANAGER_H
#define PHYSICAL_MEMORY_MANAGER_H

#include "stdint.h"
#include "Multiboot2.h"
#include "KernelConstants.h"

int PhysicalMemoryManagerInit(uint32_t multiboot_info_addr);
void* AllocPage(void);
void FreePage(void* page);
uint64_t GetFreeMemory(void);

#endif // PHYSICAL_MEMORY_MANAGER_H
