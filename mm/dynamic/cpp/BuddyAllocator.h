#pragma once

#include <stdint.h>

// Forward declaration for the C-style struct
typedef struct BuddyAllocator BuddyAllocator;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the buddy allocator with a memory region.
 *
 * @param start The start address of the memory region.
 * @param size The size of the memory region.
 */
void BuddyAllocator_Create(uint64_t start, uint64_t size);

/**
 * @brief Allocates a block of memory of at least `size` bytes.
 *
 * @param allocator The allocator instance.
 * @param size The minimum size of the block to allocate.
 * @return The address of the allocated block, or 0 if no block is available.
 */
uint64_t BuddyAllocator_Allocate(BuddyAllocator* allocator, uint64_t size);

/**
 * @brief Frees a previously allocated block of memory.
 *
 * @param allocator The allocator instance.
 * @param address The address of the block to free.
 * @param size The size of the block to free.
 */
void BuddyAllocator_Free(BuddyAllocator* allocator, uint64_t address, uint64_t size);

/**
 * @brief Dumps the state of the free list to the kernel console.
 *
 * @param allocator The allocator instance.
 */
void BuddyAllocator_DumpFreeList(BuddyAllocator* allocator);

#ifdef __cplusplus
}
#endif
