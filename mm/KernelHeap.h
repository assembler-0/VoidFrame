#ifndef KHEAP_H
#define KHEAP_H

#define KHEAP_VALIDATION_NONE 0
#define KHEAP_VALIDATION_BASIC 1
#define KHEAP_VALIDATION_FULL 2

#if defined(VF_CONFIG_HEAP_C)

#include "stdint.h"
#include "stddef.h"

void KernelHeapInit();
void* KernelMemoryAlloc(size_t size);
void* KernelAllocate(size_t num, size_t size);
void* KernelReallocate(void* ptr, size_t size);
void KernelFree(void* ptr);
void PrintHeapStats(void);
void KernelHeapSetValidationLevel(int level);  // 0=none, 1=basic, 2=full
void KernelHeapFlushCaches(void);

// Runtime tuning knobs (safe to call at early boot or quiescent points)
void KernelHeapTune(size_t small_alloc_threshold, int fast_cache_capacity);
#define KernelHeapPerfMode(mode)
#elif defined(VF_CONFIG_HEAP_RUST)

#include "../drivers/APIC/APIC.h"
#include "KernelHeapRust.h"

#define KernelHeapInit() rust_heap_enable_percpu()
#define KernelMemoryAlloc(size) rust_kmalloc(size)
#define KernelAllocate(num, size) rust_kcalloc(num, size)
#define KernelReallocate(ptr, size) rust_krealloc(ptr, size)
#define KernelFree(ptr) rust_kfree(ptr)
#define PrintHeapStats() 
#define KernelHeapSetValidationLevel(level) 
#define KernelHeapFlushCaches() rust_heap_flush_cpu(lapic_get_id())
#define KernelHeapTune(small_alloc_threshold, fast_cache_capacity)
#define KernelHeapPerfMode(mode) rust_heap_set_performance_mode(mode)
#endif

#endif // KHEAP_H