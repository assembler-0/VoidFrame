#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "stdint.h"

#define MAX_POOLS 16
#define POOL_MAGIC 0xDEADC0DE

typedef struct MemPoolBlock {
    struct MemPoolBlock* next;
} MemPoolBlock;

typedef struct MemPool {
    uint32_t magic;
    uint64_t block_size;
    uint64_t blocks_per_page;
    uint64_t total_blocks;
    uint64_t free_blocks;
    MemPoolBlock* free_list;
    void* pages;
    uint64_t num_pages;
    struct MemPool* next;
} MemPool;

// Common allocation sizes
#define POOL_SIZE_16    16
#define POOL_SIZE_32    32
#define POOL_SIZE_64    64
#define POOL_SIZE_128   128
#define POOL_SIZE_256   256
#define POOL_SIZE_512   512
#define POOL_SIZE_1024  1024
#define POOL_SIZE_2048  2048

MemPool* CreateMemPool(uint64_t block_size, uint64_t initial_pages);
void* MemPoolAlloc(MemPool* pool);
void MemPoolFree(MemPool* pool, void* ptr);
void DestroyMemPool(MemPool* pool);
void InitDefaultPools(void);

#endif