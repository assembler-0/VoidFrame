#include "KernelHeap.h"

#include "Console.h"
#include "Kernel.h"
#include "MemOps.h"
#include "Spinlock.h"
#include "VMem.h"

typedef struct HeapBlock {
    uint32_t magic;           // Magic number for corruption detection
    size_t size;              // User data size (not including header)
    uint8_t is_free;          // Boolean: 1 if free, 0 if allocated
    struct HeapBlock* next;   // Next block in list
    struct HeapBlock* prev;   // Previous block in list
    uint32_t checksum;        // Header checksum for integrity
} HeapBlock;

// Magic constants
#define HEAP_MAGIC_ALLOC 0xDEADBEEF
#define HEAP_MAGIC_FREE  0xFEEDFACE
#define MIN_BLOCK_SIZE   32
#define HEAP_ALIGN       8
#define MAX_ALLOC_SIZE   (1ULL << 30)  // 1GB limit

// Global state
static HeapBlock* heap_head = NULL;
static volatile int kheap_lock = 0;
static size_t total_allocated = 0;
static size_t peak_allocated = 0;

// Simple checksum for header integrity (exclude volatile fields)
static uint32_t ComputeChecksum(HeapBlock* block) {
    return (uint32_t)((uintptr_t)block ^ block->magic ^ block->size);
}

// Unified block validation with detailed error reporting
static int ValidateBlock(HeapBlock* block, const char* operation) {
    if (!block) {
        PrintKernelError("[HEAP] NULL block in "); PrintKernel(operation);
        return 0;
    }

    // Check magic number
    if (block->magic != HEAP_MAGIC_ALLOC && block->magic != HEAP_MAGIC_FREE) {
        PrintKernelError("[HEAP] Invalid magic "); PrintKernelHex(block->magic);
        PrintKernelError(" at "); PrintKernelHex((uint64_t)block);
        PrintKernelError(" during "); PrintKernel(operation); PrintKernel("\n");
        return 0;
    }

    // Check size bounds
    if (block->size == 0 || block->size > MAX_ALLOC_SIZE) {
        PrintKernelError("[HEAP] Invalid size "); PrintKernelInt(block->size);
        PrintKernelError(" at "); PrintKernelHex((uint64_t)block);
        PrintKernelError(" during "); PrintKernel(operation); PrintKernel("\n");
        return 0;
    }

    // Verify checksum
    uint32_t expected = ComputeChecksum(block);
    if (block->checksum != expected) {
        PrintKernelError("[HEAP] Checksum mismatch at "); PrintKernelHex((uint64_t)block);
        PrintKernelError(" during "); PrintKernel(operation);
        PrintKernelError(" (got "); PrintKernelHex(block->checksum);
        PrintKernelError(", expected "); PrintKernelHex(expected); PrintKernel(")\n");
        return 0;
    }

    return 1;
}

// Unified size alignment
static size_t AlignSize(size_t size) {
    return (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

// Get user data pointer from block
static void* BlockToUser(HeapBlock* block) {
    return (uint8_t*)block + sizeof(HeapBlock);
}

// Get block from user pointer
static HeapBlock* UserToBlock(void* ptr) {
    return (HeapBlock*)((uint8_t*)ptr - sizeof(HeapBlock));
}

// Initialize block with proper values
static void InitBlock(HeapBlock* block, size_t size, int is_free) {
    block->magic = is_free ? HEAP_MAGIC_FREE : HEAP_MAGIC_ALLOC;
    block->size = size;
    block->is_free = is_free ? 1 : 0;
    block->checksum = ComputeChecksum(block);
}

// Update checksum after modifying stable fields
static void UpdateChecksum(HeapBlock* block) {
    block->checksum = ComputeChecksum(block);
}

// Best-fit allocation for better memory utilization
static HeapBlock* FindBestFreeBlock(size_t size) {
    HeapBlock* best = NULL;
    size_t best_size = MAX_ALLOC_SIZE;

    for (HeapBlock* block = heap_head; block; block = block->next) {
        if (block->is_free && block->size >= size && block->size < best_size) {
            best = block;
            best_size = block->size;

            // Perfect fit - no need to continue
            if (block->size == size) break;
        }
    }
    return best;
}

// Split block if it's significantly larger than needed
static void SplitBlock(HeapBlock* block, size_t needed_size) {
    size_t remaining = block->size - needed_size;

    // Only split if remainder is worth it (header + minimum size)
    if (remaining < sizeof(HeapBlock) + MIN_BLOCK_SIZE) {
        return;
    }

    // Create new block from remainder
    HeapBlock* new_block = (HeapBlock*)((uint8_t*)BlockToUser(block) + needed_size);
    InitBlock(new_block, remaining - sizeof(HeapBlock), 1);

    // Link new block into list
    new_block->next = block->next;
    new_block->prev = block;
    if (block->next) block->next->prev = new_block;
    block->next = new_block;

    // Update original block size
    block->size = needed_size;
    UpdateChecksum(block);
}

// Create new block from virtual memory
static HeapBlock* CreateNewBlock(size_t size) {
    size_t total_size = sizeof(HeapBlock) + size;
    void* mem = VMemAlloc(total_size);
    if (!mem) return NULL;

    HeapBlock* block = (HeapBlock*)mem;
    InitBlock(block, size, 0);

    // Link to head of list
    block->next = heap_head;
    block->prev = NULL;
    if (heap_head) heap_head->prev = block;
    heap_head = block;

    return block;
}

// Coalesce adjacent free blocks
static void CoalesceWithAdjacent(HeapBlock* block) {
    // Merge with next block if it's free
    while (block->next && block->next->is_free) {
        HeapBlock* next = block->next;
        if (!ValidateBlock(next, "coalesce")) break;

        block->size += sizeof(HeapBlock) + next->size;
        block->next = next->next;
        if (next->next) next->next->prev = block;

        UpdateChecksum(block);
    }

    // Let previous block merge with this one if it's free
    if (block->prev && block->prev->is_free) {
        CoalesceWithAdjacent(block->prev);
    }
}

void KernelHeapInit() {
    heap_head = NULL;
    total_allocated = 0;
    peak_allocated = 0;
    PrintKernelSuccess("[HEAP] Kernel Heap Initialized\n");
}

void* KernelMemoryAlloc(size_t size) {
    // Input validation
    if (size == 0 || size > MAX_ALLOC_SIZE) {
        return NULL;
    }

    size = AlignSize(size);
    if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);

    // Try to find existing free block
    HeapBlock* block = FindBestFreeBlock(size);
    if (block) {
        if (!ValidateBlock(block, "alloc_reuse")) {
            SpinUnlockIrqRestore(&kheap_lock, flags);
            return NULL;
        }

        // Split if block is much larger than needed
        SplitBlock(block, size);
        InitBlock(block, size, 0);
    } else {
        // Create new block
        block = CreateNewBlock(size);
        if (!block) {
            SpinUnlockIrqRestore(&kheap_lock, flags);
            return NULL;
        }
    }

    // Update statistics
    total_allocated += size;
    if (total_allocated > peak_allocated) {
        peak_allocated = total_allocated;
    }

    SpinUnlockIrqRestore(&kheap_lock, flags);
    return BlockToUser(block);
}

void* KernelCallocate(size_t num, size_t size) {
    // Check for overflow
    if (num && size > MAX_ALLOC_SIZE / num) {
        return NULL;
    }

    size_t total_size = num * size;
    void* ptr = KernelMemoryAlloc(total_size);
    if (ptr) {
        FastMemset(ptr, 0, total_size);
    }
    return ptr;
}

void* KernelReallocate(void* ptr, size_t size) {
    if (!ptr) return KernelMemoryAlloc(size);
    if (size == 0) {
        KernelFree(ptr);
        return NULL;
    }

    HeapBlock* block = UserToBlock(ptr);
    if (!ValidateBlock(block, "realloc")) {
        return NULL;
    }

    if (block->is_free) {
        PrintKernelError("[HEAP] Realloc of freed memory at ");
        PrintKernelHex((uint64_t)ptr); PrintKernel("\n");
        return NULL;
    }

    size_t old_size = block->size;
    size_t aligned_size = AlignSize(size);

    // If new size fits in current block, just return it
    if (aligned_size <= old_size) {
        return ptr;
    }

    // Allocate new block and copy data
    void* new_ptr = KernelMemoryAlloc(size);
    if (!new_ptr) return NULL;

    FastMemcpy(new_ptr, ptr, old_size);
    KernelFree(ptr);
    return new_ptr;
}

void KernelFree(void* ptr) {
    if (!ptr) return;

    irq_flags_t flags = SpinLockIrqSave(&kheap_lock);

    HeapBlock* block = UserToBlock(ptr);
    if (!ValidateBlock(block, "free")) {
        SpinUnlockIrqRestore(&kheap_lock, flags);
        return;
    }

    // Check for double free
    if (block->is_free) {
        SpinUnlockIrqRestore(&kheap_lock, flags);
        PrintKernelError("[HEAP] Double free at ");
        PrintKernelHex((uint64_t)ptr); PrintKernel("\n");
        return;
    }

    // Security: zero user data
    FastMemset(ptr, 0, block->size);

    // Mark as free and update statistics
    InitBlock(block, block->size, 1);
    total_allocated -= block->size;

    // Coalesce with adjacent free blocks
    CoalesceWithAdjacent(block);

    SpinUnlockIrqRestore(&kheap_lock, flags);
}

void PrintHeapStats(void) {
     const irq_flags_t flags = SpinLockIrqSave(&kheap_lock);

    size_t free_blocks = 0, used_blocks = 0;
    size_t free_bytes = 0, used_bytes = 0;
    size_t corrupted = 0;

    for (HeapBlock* block = heap_head; block; block = block->next) {
        if (!ValidateBlock(block, "stats")) {
            corrupted++;
            continue;
        }

        if (block->is_free) {
            free_blocks++;
            free_bytes += block->size;
        } else {
            used_blocks++;
            used_bytes += block->size;
        }
    }

    SpinUnlockIrqRestore(&kheap_lock, flags);
    
    PrintKernel("[HEAP] Blocks: "); PrintKernelInt(used_blocks);
    PrintKernel(" used, "); PrintKernelInt(free_blocks); PrintKernel(" free\n");
    PrintKernel("[HEAP] Memory: "); PrintKernelInt(used_bytes);
    PrintKernel(" used, "); PrintKernelInt(free_bytes); PrintKernel(" free\n");
    PrintKernel("[HEAP] Peak allocated: "); PrintKernelInt(peak_allocated);
    PrintKernel(", corrupted blocks: "); PrintKernelInt(corrupted); PrintKernel("\n");
}