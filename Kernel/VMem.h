#ifndef VMEM_H
#define VMEM_H

#include "stdint.h"

#define VIRT_ADDR_SPACE_START 0x400000000000ULL // 64TB
#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITABLE   (1ULL << 1)
#define PAGE_USER       (1ULL << 2)

typedef struct {
    uint64_t* pml4;
    uint64_t next_vaddr;
} VirtAddrSpace;

void VMemInit(void);
void* VMemAlloc(uint64_t size);
void VMemFree(void* vaddr, uint64_t size);
int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags);

#endif