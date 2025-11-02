#include <mm/dynamic/cpp/BuddyAllocator.h>
#include <VMem.h>
#include <Console.h>
#include <../../../kernel/atomic/cpp/Spinlock.h>

// Buddy allocator constants
#define BUDDY_MIN_ORDER 12  // 4KB pages
#define BUDDY_MAX_ORDER 30  // 1GB max allocation
#define BUDDY_NUM_ORDERS (BUDDY_MAX_ORDER - BUDDY_MIN_ORDER + 1)

// Hash table for fast buddy lookup
#define HASH_TABLE_SIZE 4096 // Must be a power of 2

// Pre-allocated pool for buddy nodes
#define MAX_BUDDY_NODES 2048

typedef struct VMemFreeBlock {
    uint64_t base;
    uint64_t size;
    struct VMemFreeBlock* next;
    struct VMemFreeBlock* prev;
    struct VMemFreeBlock* hnext; // For hash table chaining
} VMemFreeBlock;

struct BuddyAllocator {
    VMemFreeBlock* free_lists[BUDDY_NUM_ORDERS];
    VMemFreeBlock* hash_table[HASH_TABLE_SIZE];
    VMemFreeBlock node_pool[MAX_BUDDY_NODES];
    VMemFreeBlock* node_head;
    Spinlock lock;
};

BuddyAllocator g_buddy_allocator;

static uint32_t HashAddress(uint64_t addr) {
    return static_cast<uint32_t>((addr >> BUDDY_MIN_ORDER) * 2654435761) & (HASH_TABLE_SIZE - 1);
}

static uint32_t GetOrder(uint64_t size) {
    if (size <= PAGE_SIZE) return 0;
    return 64 - __builtin_clzll(size - 1) - BUDDY_MIN_ORDER;
}

static uint64_t OrderToSize(uint32_t order) {
    return 1ULL << (order + BUDDY_MIN_ORDER);
}

static void InitBuddyNodePool(BuddyAllocator* allocator) {
    allocator->node_head = &allocator->node_pool[0];
    for (int i = 0; i < MAX_BUDDY_NODES - 1; ++i) {
        allocator->node_pool[i].next = &allocator->node_pool[i + 1];
    }
    allocator->node_pool[MAX_BUDDY_NODES - 1].next = nullptr;
}

static VMemFreeBlock* AllocBuddyNode(BuddyAllocator* allocator) {
    if (!allocator->node_head) return nullptr;
    VMemFreeBlock* node = allocator->node_head;
    allocator->node_head = node->next;
    return node;
}

static void ReleaseBuddyNode(BuddyAllocator* allocator, VMemFreeBlock* node) {
    node->next = allocator->node_head;
    allocator->node_head = node;
}

static void AddFreeBlock(BuddyAllocator* allocator, uint64_t addr, uint32_t order) {
    if (order >= BUDDY_NUM_ORDERS) return;

    VMemFreeBlock* node = AllocBuddyNode(allocator);
    if (!node) return;

    node->base = addr;
    node->size = OrderToSize(order);
    node->prev = nullptr;
    node->hnext = nullptr;

    node->next = allocator->free_lists[order];
    if (allocator->free_lists[order]) {
        allocator->free_lists[order]->prev = node;
    }
    allocator->free_lists[order] = node;

    uint32_t hash = HashAddress(addr);
    node->hnext = allocator->hash_table[hash];
    allocator->hash_table[hash] = node;
}

static VMemFreeBlock* FindFreeBlock(BuddyAllocator* allocator, uint64_t addr, uint32_t order) {
    uint32_t hash = HashAddress(addr);
    VMemFreeBlock* curr = allocator->hash_table[hash];
    uint64_t size = OrderToSize(order);

    while (curr) {
        if (curr->base == addr && curr->size == size) {
            return curr;
        }
        curr = curr->hnext;
    }
    return nullptr;
}

static void RemoveFreeBlock(BuddyAllocator* allocator, VMemFreeBlock* node, uint32_t order) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        allocator->free_lists[order] = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }

    uint32_t hash = HashAddress(node->base);
    VMemFreeBlock* prev_h = nullptr;
    VMemFreeBlock* curr_h = allocator->hash_table[hash];
    while (curr_h) {
        if (curr_h == node) {
            if (prev_h) {
                prev_h->hnext = curr_h->hnext;
            } else {
                allocator->hash_table[hash] = curr_h->hnext;
            }
            break;
        }
        prev_h = curr_h;
        curr_h = curr_h->hnext;
    }

    ReleaseBuddyNode(allocator, node);
}

void BuddyAllocator_Create(uint64_t start, uint64_t size) {
    for (auto& list : g_buddy_allocator.free_lists) {
        list = nullptr;
    }
    for (auto& entry : g_buddy_allocator.hash_table) {
        entry = nullptr;
    }

    InitBuddyNodePool(&g_buddy_allocator);

    uint64_t addr = start;
    uint64_t remaining = size;
    while (remaining >= PAGE_SIZE) {
        uint32_t order = BUDDY_NUM_ORDERS - 1;
        while (order > 0 && OrderToSize(order) > remaining) {
            order--;
        }
        AddFreeBlock(&g_buddy_allocator, addr, order);
        uint64_t block_size = OrderToSize(order);
        addr += block_size;
        remaining -= block_size;
    }
}

uint64_t BuddyAllocator_Allocate(BuddyAllocator* allocator, uint64_t size) {
    SpinlockGuard lock(allocator->lock);

    uint32_t order = GetOrder(size);
    if (order >= BUDDY_NUM_ORDERS) {
        return 0;
    }

    for (uint32_t curr_order = order; curr_order < BUDDY_NUM_ORDERS; curr_order++) {
        VMemFreeBlock* block = allocator->free_lists[curr_order];
        if (!block) continue;

        RemoveFreeBlock(allocator, block, curr_order);

        uint64_t addr = block->base;

        while (curr_order > order) {
            curr_order--;
            uint64_t buddy_addr = addr + OrderToSize(curr_order);
            AddFreeBlock(allocator, buddy_addr, curr_order);
        }

        return addr;
    }

    return 0;
}

void BuddyAllocator_Free(BuddyAllocator* allocator, uint64_t address, uint64_t size) {
    SpinlockGuard lock(allocator->lock);

    uint32_t order = GetOrder(size);
    if (order >= BUDDY_NUM_ORDERS) {
        return;
    }

    while (order < BUDDY_NUM_ORDERS - 1) {
        uint64_t buddy_addr = address ^ OrderToSize(order);
        VMemFreeBlock* buddy = FindFreeBlock(allocator, buddy_addr, order);

        if (!buddy) break;

        RemoveFreeBlock(allocator, buddy, order);

        if (buddy_addr < address) {
            address = buddy_addr;
        }
        order++;
    }

    AddFreeBlock(allocator, address, order);
}

void BuddyAllocator_DumpFreeList(BuddyAllocator* allocator) {
    SpinlockGuard lock(allocator->lock);
    PrintKernel("[VMEM] Buddy Allocator Free Blocks:\n");

    uint64_t total_free = 0;
    for (uint32_t order = 0; order < BUDDY_NUM_ORDERS; order++) {
        uint32_t count = 0;
        VMemFreeBlock* current = allocator->free_lists[order];
        while (current) {
            count++;
            current = current->next;
        }

        if (count > 0) {
            uint64_t block_size = OrderToSize(order);
            uint64_t total_size = count * block_size;
            total_free += total_size;

            PrintKernel("  Order "); PrintKernelInt(order);
            PrintKernel(" ("); PrintKernelInt(block_size / 1024); PrintKernel("KB): ");
            PrintKernelInt(count); PrintKernel(" blocks, ");
            PrintKernelInt(total_size / (1024 * 1024)); PrintKernel("MB total\n");
        }
    }

    PrintKernel("[VMEM] Total free: "); PrintKernelInt(total_free / (1024 * 1024)); PrintKernel("MB\n");
}
