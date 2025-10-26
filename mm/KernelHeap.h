#ifndef KHEAP_H
#define KHEAP_H

#define KHEAP_VALIDATION_NONE 0
#define KHEAP_VALIDATION_BASIC 1
#define KHEAP_VALIDATION_FULL 2

#include "stdint.h"
#include "stddef.h"

#if defined(VF_CONFIG_HEAP_C)

#include "Magazine.h"

#define KernelHeapInit() MagazineInit()
#define KernelMemoryAlloc(size) MagazineAlloc(size)
#define KernelAllocate(num, size) MagazineAllocate(num, size)
#define KernelReallocate(ptr, size) MagazineReallocate(ptr, size)
#define KernelFree(ptr) MagazineFree(ptr)
#define PrintHeapStats() MagazinePrintStats()
#define KernelHeapSetValidationLevel(level) MagazineSetValidationLevel(level)
#define KernelHeapFlushCaches() MagazineFlushCaches()
#define KernelHeapTune(small_alloc_threshold, fast_cache_capacity)
#define KernelHeapPerfMode(mode) MagazineSetPerfMode(mode)

#elif defined(VF_CONFIG_HEAP_RUST)

#include "APIC.h"
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

#elif defined(VF_CONFIG_HEAP_HYBRID)

#include "APIC.h"
#include "Magazine.h"
#include "KernelHeapRust.h"

// Threshold for routing to Rust small-object allocator
#ifndef HYBRID_SMALL_THRESHOLD
#define HYBRID_SMALL_THRESHOLD 512
#endif

#define KernelHeapInit() do { MagazineInit(); rust_heap_enable_percpu(); } while (0)
#define KernelMemoryAlloc(size) ((size) <= HYBRID_SMALL_THRESHOLD ? MagazineAlloc(size) : rust_kmalloc(size))
#define KernelAllocate(num, size) ((size) <= HYBRID_SMALL_THRESHOLD ? MagazineAllocate(num, size) : rust_kcalloc(num, size))
#define KernelReallocate(ptr, size) MagazineReallocate((ptr), (size))
// Free may receive pointers from either backend; MagazineFree forwards unknown blocks to Rust in HYBRID mode.
#define KernelFree(ptr) MagazineFree((ptr))
#define PrintHeapStats() MagazinePrintStats()
#define KernelHeapSetValidationLevel(level) MagazineSetValidationLevel((level))
#define KernelHeapFlushCaches() do { MagazineFlushCaches(); rust_heap_flush_cpu(lapic_get_id()); } while (0)
#define KernelHeapTune(small_alloc_threshold, fast_cache_capacity)
#define KernelHeapPerfMode(mode) do { MagazineSetPerfMode((mode)); rust_heap_set_performance_mode((mode)); } while (0)

#endif

#endif // KHEAP_H