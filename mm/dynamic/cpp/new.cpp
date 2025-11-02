#include <KernelHeap.h>

void* operator new(unsigned long size) {
    return KernelMemoryAlloc(size);
}

void operator delete(void* ptr) {
    KernelFree(ptr);
}
