#ifndef KERNEL_HEAP_RUST_H
#define KERNEL_HEAP_RUST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Core allocation functions
void* rust_kmalloc(size_t size);
void rust_kfree(void* ptr);
void* rust_krealloc(void* ptr, size_t new_size);
void* rust_kcalloc(size_t count, size_t size);

// Statistics and monitoring
typedef struct {
    size_t total_allocated;
    size_t peak_allocated;
    uint64_t alloc_count;
    uint64_t free_count;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t coalesce_count;
    uint64_t corruption_count;
} HeapStats;

void rust_heap_get_stats(HeapStats* stats);
int rust_heap_validate(void);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_HEAP_RUST_H