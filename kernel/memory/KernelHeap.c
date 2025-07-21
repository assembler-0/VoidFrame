#include "KernelHeap.h"
#include "VMem.h"
#include "RustKernelHeap.h"

#define KERNEL_HEAP_SIZE (16 * 1024 * 1024)

void KernelHeapInit() {
    CLayout heap_layout = {KERNEL_HEAP_SIZE, 4096}; // Allocate 16MB for the heap, page aligned
    void* heap_start = RustKernelMemoryAlloc(heap_layout);
    if (heap_start) {
        RustKernelHeapInit(heap_start, KERNEL_HEAP_SIZE);
    }
}

void* KernelMemoryAlloc(size_t size) {
    if (size == 0) return NULL;
    CLayout layout = {size, 8}; // Assuming 8-byte alignment for simplicity
    return RustKernelMemoryAlloc(layout);
}

void* KernelCallLocation(size_t num, size_t size) {
    size_t total_size = num * size;
    if (total_size == 0) return NULL;
    return RustKernelCallLocation(total_size, 8);
}

void* KernelRealLocation(void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return KernelMemoryAlloc(new_size);
    }
    if (new_size == 0) {
        // We can't get the old size, so we can't call RustKernelFree.
        // This is a limitation of the current design.
        return NULL;
    }
    // We can't get the old layout, so we can't call RustKernelRealLocation.
    // This is a limitation of the current design.
    void* new_ptr = KernelMemoryAlloc(new_size);
    if (new_ptr) {
        // We can't get the old size, so we can't do a safe copy.
        // This is a limitation of the current design.
    }
    return new_ptr;
}

void KernelFree(void* ptr) {
    // We can't get the layout, so we can't call RustKernelFree.
    // This is a limitation of the current design.
    (void)ptr;
}
