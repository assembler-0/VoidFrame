#ifndef KHEAP_H
#define KHEAP_H

#include "stdint.h"
#include "stddef.h"

void KernelHeapInit();
void* KernelMemoryAlloc(size_t size);
void* KernelCallocate(size_t num, size_t size);
void* KernelReallocate(void* ptr, size_t size);
void KernelFree(void* ptr);
void PrintHeapStats(void);

#endif // KHEAP_H