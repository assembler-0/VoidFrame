#ifndef KHEAP_H
#define KHEAP_H

#define KHEAP_VALIDATION_NONE 0
#define KHEAP_VALIDATION_BASIC 1
#define KHEAP_VALIDATION_FULL 2

#include <stdint.h>
#include <stddef.h>
#include <VMem.h>
#include <APIC.h>
#include <Magazine.h>
#include <KernelHeapRust.h>

// Tier 1: C Magazine Allocator for extreme speed on small allocations
#define MAGAZINE_MAX_SIZE 1024

// Tier 2: Rust Allocator for general-purpose medium-sized allocations
#define RUST_MAX_SIZE (64 * 1024)

// Helper function to wrap large VMem allocations with a header
static inline void* LargeBlockAlloc(size_t size) {
    size_t total_size = sizeof(LargeBlockHeader) + size;
    void* raw_mem = VMemAlloc(total_size);
    if (!raw_mem) {
        return NULL;
    }
    LargeBlockHeader* header = (LargeBlockHeader*)raw_mem;
    header->magic = LARGE_BLOCK_MAGIC;
    header->size = size;
    return (void*)(header + 1);
}

// Dispatcher for freeing memory based on header magic numbers
static inline void HybridFree(void* ptr) {
    if (!ptr) {
        return;
    }

    // Check for small magazine allocation
    MagazineBlockHeader* small_header = (MagazineBlockHeader*)ptr - 1;
    if (small_header->magic == MAGAZINE_BLOCK_MAGIC) {
        MagazineFree(ptr);
        return;
    }

    // Check for large VMem allocation
    LargeBlockHeader* large_header = (LargeBlockHeader*)ptr - 1;
    if (large_header->magic == LARGE_BLOCK_MAGIC) {
        VMemFree((void*)large_header, large_header->size + sizeof(LargeBlockHeader));
        return;
    }

    // Otherwise, assume it's from the Rust allocator
    rust_kfree(ptr);
}


#define KernelHeapInit() do { MagazineInit(); rust_heap_enable_percpu(); } while (0)

#define KernelMemoryAlloc(size) \
    ((size) <= MAGAZINE_MAX_SIZE) ? MagazineAlloc(size) : \
    ((size) <= RUST_MAX_SIZE) ? rust_kmalloc(size) : \
    LargeBlockAlloc(size)

#define KernelAllocate(num, size) KernelMemoryAlloc((num)*(size)) // Simplified; assumes no overflow

#define KernelReallocate(ptr, size) MagazineReallocate((ptr), (size)) // Realloc remains complex, handled by Magazine for now

#define KernelFree(ptr) HybridFree(ptr)

#define PrintHeapStats() MagazinePrintStats()
#define KernelHeapSetValidationLevel(level) MagazineSetValidationLevel((level))
#define KernelHeapFlushCaches() do { MagazineFlushCaches(); rust_heap_flush_cpu(lapic_get_id()); } while (0)
#define KernelHeapTune(small_alloc_threshold, fast_cache_capacity)
#define KernelHeapPerfMode(mode) do { MagazineSetPerfMode((mode)); rust_heap_set_performance_mode((mode)); } while (0)

#endif // KHEAP_H