#ifndef KHEAP_H
#define KHEAP_H

#include "stdint.h"
#include "stddef.h"

#define KHEAP_VALIDATION_NONE 0
#define KHEAP_VALIDATION_BASIC 1
#define KHEAP_VALIDATION_FULL 2

void KernelHeapInit();
void* KernelMemoryAlloc(size_t size);
void* KernelAllocate(size_t num, size_t size);
void* KernelReallocate(void* ptr, size_t size);
void KernelFree(void* ptr);
void PrintHeapStats(void);
void KernelHeapSetValidationLevel(int level);  // 0=none, 1=basic, 2=full
void KernelHeapFlushCaches(void);

#endif // KHEAP_H