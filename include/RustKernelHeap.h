#ifndef RUST_KERNEL_HEAP_H
#define RUST_KERNEL_HEAP_H

#include "stdint.h"
#include "stddef.h"

// C-compatible layout for Rust's Layout struct
typedef struct {
    size_t size;
    size_t align;
} CLayout;

// Heap functions
extern void RustKernelHeapInit(void* heap_start, size_t heap_size);
extern void* RustKernelMemoryAlloc(CLayout layout);
extern void RustKernelFree(void* ptr, CLayout layout);
extern void* RustKernelCallLocation(size_t size, size_t align);
extern void* RustKernelRealLocation(void* ptr, CLayout old_layout, size_t new_size);

// VMM functions
extern int RustVMemInit();
extern int RustVMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags);
extern uint64_t RustVMemGetPML4PhysAddr();

// Console functions
extern void PrintKernel(const char* str);

#endif // RUST_KERNEL_HEAP_H