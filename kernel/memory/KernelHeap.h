#ifndef KHEAP_H
#define KHEAP_H

#include "stdint.h"
#include "stddef.h"

void KernelHeapInit();
void* KernelMemoryAlloc(size_t size);
void* KernelCallLocation(size_t num, size_t size);
void* KernelRealLocation(void* ptr, size_t size);
void KernelFree(void* ptr);

#endif // KHEAP_H