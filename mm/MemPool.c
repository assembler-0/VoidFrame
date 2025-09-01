#include "MemPool.h"
#include "VMem.h"

static MemPool* default_pools[MAX_POOLS];
static int num_default_pools = 0;

MemPool* CreateMemPool(uint64_t block_size, uint64_t initial_pages) {
    MemPool* pool = (MemPool*)VMemAlloc(sizeof(MemPool));
    if (!pool) return NULL;

    pool->magic = POOL_MAGIC;
    pool->block_size = block_size;
    pool->blocks_per_page = PAGE_SIZE / block_size;
    pool->total_blocks = 0;
    pool->free_blocks = 0;
    pool->free_list = NULL;
    pool->num_pages = 0;

    // Allocate initial pages
    if (initial_pages > 0) {
        pool->pages = VMemAlloc(initial_pages * PAGE_SIZE);
        if (!pool->pages) {
            VMemFree(pool, sizeof(MemPool));
            return NULL;
        }

        pool->num_pages = initial_pages;
        pool->total_blocks = initial_pages * pool->blocks_per_page;
        pool->free_blocks = pool->total_blocks;

        // Initialize free list
        uint8_t* block_ptr = (uint8_t*)pool->pages;
        for (uint64_t i = 0; i < pool->total_blocks - 1; i++) {
            ((MemPoolBlock*)block_ptr)->next = (MemPoolBlock*)(block_ptr + block_size);
            block_ptr += block_size;
        }
        ((MemPoolBlock*)block_ptr)->next = NULL;
        pool->free_list = (MemPoolBlock*)pool->pages;
    }

    return pool;
}

void* MemPoolAlloc(MemPool* pool) {
    if (!pool || pool->magic != POOL_MAGIC || !pool->free_list) {
        return NULL;
    }

    MemPoolBlock* block = pool->free_list;
    pool->free_list = block->next;
    pool->free_blocks--;

    return (void*)block;
}

void MemPoolFree(MemPool* pool, void* ptr) {
    if (!pool || !ptr || pool->magic != POOL_MAGIC) return;

    MemPoolBlock* block = (MemPoolBlock*)ptr;
    block->next = pool->free_list;
    pool->free_list = block;
    pool->free_blocks++;
}

void InitDefaultPools(void) {
    static const uint64_t sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    static const uint64_t initial_pages[] = {4, 4, 4, 2, 2, 2, 1, 1};

    for (int i = 0; i < 8 && num_default_pools < MAX_POOLS; i++) {
        default_pools[num_default_pools] = CreateMemPool(sizes[i], initial_pages[i]);
        if (default_pools[num_default_pools]) {
            num_default_pools++;
        }
    }
}

void* FastAlloc(uint64_t size) {
    // Find appropriate pool
    for (int i = 0; i < num_default_pools; i++) {
        if (default_pools[i]->block_size >= size) {
            return MemPoolAlloc(default_pools[i]);
        }
    }

    // Fall back to regular allocation for large sizes
    return VMemAlloc(size);
}