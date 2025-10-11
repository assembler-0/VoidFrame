#ifndef KERNEL_HEAP_RUST_H
#define KERNEL_HEAP_RUST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Heap statistics structure
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

// Main allocation API (with per-CPU optimization)
void* rust_kmalloc(size_t size);
void rust_kfree(void* ptr);
void* rust_krealloc(void* ptr, size_t new_size);
void* rust_kcalloc(size_t count, size_t size);

// Per-CPU cache control
void rust_heap_enable_percpu(void);
void rust_heap_disable_percpu(void);
void rust_heap_flush_cpu(size_t cpu);
void rust_heap_get_percpu_stats(size_t cpu, uint64_t* hits, uint64_t* misses);

// Heap management
void rust_heap_get_stats(HeapStats* stats);
int rust_heap_validate(void);
void rust_heap_set_performance_mode(uint64_t mode);

#ifdef __cplusplus
}
#endif

#endif // KERNEL_HEAP_RUST_H