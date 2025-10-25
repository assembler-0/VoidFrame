#ifndef MAGAZINE_HEAP_H
#define MAGAZINE_HEAP_H

#include "stdint.h"
#include "stddef.h"
#include "SpinlockRust.h"

// =================================================================================================
// Constants and Configuration
// =================================================================================================

// The maximum number of CPU cores the allocator will support.
#define MAX_CPU_CORES 64

// The number of blocks a single magazine can hold. A smaller size reduces memory hoarding.
#define MAGAZINE_CAPACITY 32

// The number of distinct size classes for small allocations.
#define NUM_SIZE_CLASSES 8

// The maximum size of an allocation to be handled by the magazine system.
// Larger allocations will be routed to a fallback allocator (VMem).
#define MAX_SMALL_ALLOC_SIZE 1024

// Size classes for small allocations. These should be chosen to minimize fragmentation.
// Powers of 2 are simple, but other schemes can be more efficient.
static const size_t size_classes[NUM_SIZE_CLASSES] = {
    16, 32, 64, 128, 256, 512, 768, 1024
};

// The size of a slab in pages. This determines how many blocks a slab can hold.
// A larger slab size reduces VMemAlloc calls but can increase internal fragmentation.
#define SLAB_PAGES 4 // 4 * 4KB = 16KB per slab
#define SLAB_SIZE (SLAB_PAGES * PAGE_SIZE)

// =================================================================================================
// Core Data Structures
// =================================================================================================

/**
 * @brief Header for large allocations that bypass the magazine system.
 * This allows tracking the size of such allocations for proper freeing.
 */
typedef struct LargeBlockHeader {
    size_t size; // The original requested size of the allocation.
} LargeBlockHeader;

/**
 * @brief A "magazine" of free memory blocks.
 * This is the core of the lock-free fast path. It's a simple LIFO stack.
 */
typedef struct Magazine {
    void* blocks[MAGAZINE_CAPACITY]; // Pointers to free blocks.
    uint32_t count;                  // Number of free blocks currently in the magazine.
    struct Magazine* next;           // For linking magazines in depot lists.
} Magazine;

/**
 * @brief Per-CPU cache of active magazines.
 * Each CPU has one of these structures. Access to this is local to the CPU
 * and does not require locking.
 */
typedef struct PerCpuCache {
    Magazine* active_magazines[NUM_SIZE_CLASSES]; // One active magazine for each size class.
} PerCpuCache;

/**
 * @brief A large slab of memory from which blocks of a certain size are carved.
 * Slabs are managed by the central Depot.
 */
typedef struct Slab {
    struct Slab* next;          // Slabs for a size class are kept in a linked list.
    void* base_ptr;             // The start of the memory region allocated from VMem.
    size_t block_size;          // The size of blocks this slab provides.
    int size_class_index;       // The size class this slab belongs to.
    uint32_t total_blocks;      // Total number of blocks this slab can hold.
    uint32_t free_blocks;       // Number of free blocks remaining in this slab.
    void* free_list_head;       // Head of a free list within the slab itself.
} Slab;

/**
 * @brief Manages magazines and slabs for a single size class within the Depot.
 */
typedef struct SizeClassDepot {
    Magazine* full_magazines;    // Linked list of full magazines returned by CPUs.
    Magazine* partial_magazines; // Linked list of magazines with some free blocks.
    Magazine* empty_magazines;   // Linked list of empty magazines ready to be filled.
    Slab* slabs;                 // Linked list of slabs for this size class.
} SizeClassDepot;

/**
 * @brief The central depot for the heap.
 * This structure is shared across all CPUs and all accesses to it must be
 * protected by a spinlock. It acts as the central supply store for magazines.
 */
typedef struct Depot {
    RustSpinLock* lock;
    SizeClassDepot size_class_depots[NUM_SIZE_CLASSES];
} Depot;


// =================================================================================================
// Global Variables
// =================================================================================================

// The array of per-CPU caches. Indexed by CPU ID.
extern PerCpuCache per_cpu_caches[MAX_CPU_CORES];

// The single, global depot.
extern Depot depot;


void MagazineInit();
void* MagazineAlloc(size_t size);
void MagazineFree(void* ptr);
void* MagazineAllocate(size_t num, size_t size);
void* MagazineReallocate(void* ptr, size_t size);

#endif // MAGAZINE_HEAP_H
