#include "KernelHeap.h"
#include "Console.h"
#include "MemOps.h"
#include "Panic.h"
#include "VMem.h"
#include "SpinlockRust.h"

typedef struct HeapBlock {
    uint32_t magic;           // Magic number for corruption detection
    size_t size;              // User data size (not including header)
    uint8_t is_free;          // Boolean: 1 if free, 0 if allocated
    uint8_t in_cache;         // Boolean: 1 if present in fast cache
    struct HeapBlock* next;   // Next block in heap list (by physical order within a chunk)
    struct HeapBlock* prev;   // Previous block in heap list
    uint32_t checksum;        // Header checksum for integrity
    struct HeapBlock* cache_next; // Next block in fast cache list (separate linkage)
} HeapBlock;

// Magic constants
#define HEAP_MAGIC_ALLOC 0xDEADBEEF
#define HEAP_MAGIC_FREE  0xFEEDFACE
#define MIN_BLOCK_SIZE   32
#define HEAP_ALIGN       8
#define MAX_ALLOC_SIZE   (1ULL << 30)  // 1GB limit

// Improved size class allocation
#define SMALL_ALLOC_THRESHOLD 1024
#define NUM_SIZE_CLASSES 12
#define FAST_CACHE_SIZE 32

// Optimized size classes: powers of 2 + midpoints for better fit
static const size_t size_classes[NUM_SIZE_CLASSES] = {
    32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536
};

// Enhanced fast allocation cache with statistics
typedef struct {
    HeapBlock* free_list;
    int count;
    uint64_t hits;           // Cache hit counter
    uint64_t misses;         // Cache miss counter
} FastCache;

// Global state
static HeapBlock* heap_head = NULL;
static RustSpinLock* kheap_lock = 0;
static size_t total_allocated = 0;
static size_t peak_allocated = 0;
static FastCache fast_caches[NUM_SIZE_CLASSES];
static uint64_t alloc_counter = 0;  // For periodic coalescing

// Runtime-tunable knobs (with safe defaults)
static volatile size_t g_small_alloc_threshold = SMALL_ALLOC_THRESHOLD;
static volatile int g_fast_cache_capacity = FAST_CACHE_SIZE;

// Validation level (can be reduced in production)
static volatile int validation_level = 1; // 0=none, 1=basic, 2=full

// Forward declaration
static HeapBlock* CoalesceBlock(HeapBlock* block);

// Simple checksum for header integrity
static uint32_t ComputeChecksum(HeapBlock* block) {
    return (uint32_t)((uintptr_t)block ^ block->magic ^ block->size);
}

// Fast validation (only checks magic)
static inline int ValidateBlockFast(HeapBlock* block) {
    return block && (block->magic == HEAP_MAGIC_ALLOC || block->magic == HEAP_MAGIC_FREE);
}

// Full validation with detailed error reporting
static int ValidateBlockFull(HeapBlock* block, const char* operation) {
    if (!block) {
        PrintKernelError("[HEAP] NULL block in "); PrintKernel(operation);
        return 0;
    }

    if (block->magic != HEAP_MAGIC_ALLOC && block->magic != HEAP_MAGIC_FREE) {
        PrintKernelError("[HEAP] Invalid magic during "); PrintKernel(operation); PrintKernel("\n");
        return 0;
    }

    if (block->size == 0 || block->size > MAX_ALLOC_SIZE) {
        PrintKernelError("[HEAP] Invalid size during "); PrintKernel(operation); PrintKernel("\n");
        return 0;
    }

    if (validation_level > 1) {
        uint32_t expected = ComputeChecksum(block);
        if (block->checksum != expected) {
            PrintKernelError("[HEAP] Checksum mismatch during "); PrintKernel(operation); PrintKernel("\n");
            return 0;
        }
    }

    return 1;
}

// Adaptive validation
static inline int ValidateBlock(HeapBlock* block, const char* operation) {
    if (validation_level == 0) return 1;
    if (validation_level == 1) return ValidateBlockFast(block);
    return ValidateBlockFull(block, operation);
}

// Get size class index for small allocations
static int GetSizeClass(size_t size) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= size_classes[i]) {
            return i;
        }
    }
    return -1; // Not a small allocation
}

// Unified size alignment
static inline size_t AlignSize(size_t size) {
    return (size + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

// Get user data pointer from block
static inline void* BlockToUser(HeapBlock* block) {
    return (uint8_t*)block + sizeof(HeapBlock);
}

// Get block from user pointer
static inline HeapBlock* UserToBlock(void* ptr) {
    return (HeapBlock*)((uint8_t*)ptr - sizeof(HeapBlock));
}

// Initialize block with proper values
static void InitBlock(HeapBlock* block, size_t size, int is_free) {
    block->magic = is_free ? HEAP_MAGIC_FREE : HEAP_MAGIC_ALLOC;
    block->size = size;
    block->is_free = is_free ? 1 : 0;
    block->in_cache = 0;           // Reset cache state on (re)initialization
    block->cache_next = NULL;      // Clear cache linkage
    if (validation_level > 1) {
        block->checksum = ComputeChecksum(block);
    }
}

// Update checksum after modifying stable fields
static inline void UpdateChecksum(HeapBlock* block) {
    if (validation_level > 1) {
        block->checksum = ComputeChecksum(block);
    }
}

// Pop block from fast cache
static HeapBlock* FastCachePop(int size_class) {
    ASSERT(__sync_fetch_and_add(&kheap_lock, 0) != 0);
    FastCache* cache = &fast_caches[size_class];
    if (!cache->free_list) return NULL;

    HeapBlock* block = cache->free_list;
    cache->free_list = block->cache_next;
    cache->count--;

    // Clear cache linkage and flag
    block->cache_next = NULL;
    block->in_cache = 0;
    return block;
}

// Push block to fast cache (if not full)
static void FastCachePush(HeapBlock* block, int size_class) {
    ASSERT(__sync_fetch_and_add(&kheap_lock, 0) != 0);
    FastCache* cache = &fast_caches[size_class];
    
    if (cache->count >= g_fast_cache_capacity) {
        // Cache full - coalesce instead of caching
        CoalesceBlock(block);
        return;
    }

    block->cache_next = cache->free_list;
    cache->free_list = block;
    cache->count++;
    block->in_cache = 1;
}

// Optimized free block search with early termination
static HeapBlock* FindFreeBlock(size_t size) {
    // For small allocations, do a quick scan for exact/close fits
    if (size <= g_small_alloc_threshold) {
        HeapBlock* first_fit = NULL;
        int blocks_scanned = 0;

        for (HeapBlock* block = heap_head; block && blocks_scanned < 32; block = block->next, blocks_scanned++) {
            if (block->is_free && !block->in_cache && block->size >= size) {
                if (block->size <= size * 2) { // Close fit
                    return block;
                }
                if (!first_fit) first_fit = block; // Remember first fit
            }
        }
        return first_fit;
    }

    // For larger allocations, do best-fit search
    HeapBlock* best = NULL;
    size_t best_size = MAX_ALLOC_SIZE;

    for (HeapBlock* block = heap_head; block; block = block->next) {
        if (block->is_free && !block->in_cache && block->size >= size && block->size < best_size) {
            best = block;
            best_size = block->size;
            if (block->size == size) break; // Perfect fit
        }
    }
    return best;
}

// Split block if it's significantly larger than needed
static void SplitBlock(HeapBlock* block, size_t needed_size) {
    size_t remaining = block->size - needed_size;

    // Only split if remainder is worth it
    if (remaining < sizeof(HeapBlock) + MIN_BLOCK_SIZE) {
        return;
    }

    HeapBlock* new_block = (HeapBlock*)((uint8_t*)BlockToUser(block) + needed_size);
    InitBlock(new_block, remaining - sizeof(HeapBlock), 1);

    // Link new block into list
    new_block->next = block->next;
    new_block->prev = block;
    if (block->next) block->next->prev = new_block;
    block->next = new_block;

    // Update original
    block->size = needed_size;
    UpdateChecksum(block);
}

// Create new block from virtual memory with size optimization
static HeapBlock* CreateNewBlock(size_t size) {
    // For small allocations, allocate larger chunks to reduce VMem calls
    size_t chunk_size = size;
    if (size <= g_small_alloc_threshold) {
        chunk_size = (size < 4096) ? 4096 : PAGE_ALIGN_UP(size * 4);
    }

    size_t total_size = sizeof(HeapBlock) + chunk_size;
    void* mem = VMemAlloc(total_size);
    if (!mem) return NULL;

    HeapBlock* block = (HeapBlock*)mem;
    InitBlock(block, chunk_size, 0);

    // Link to head of list
    block->next = heap_head;
    block->prev = NULL;
    if (heap_head) heap_head->prev = block;
    heap_head = block;

    // If we allocated more than needed, split the block
    if (chunk_size > size) {
        SplitBlock(block, size);
    }

    return block;
}

// Physical adjacency helper
static inline int AreAdjacent(HeapBlock* a, HeapBlock* b) {
    return (uint8_t*)b == ((uint8_t*)BlockToUser(a) + a->size);
}

// Remove a block from any fast cache it may be in
static void CacheRemove(HeapBlock* blk) {
    if (!blk->in_cache) return;
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        HeapBlock* prev = NULL;
        HeapBlock* cur = fast_caches[i].free_list;
        while (cur) {
            if (cur == blk) {
                if (prev) prev->cache_next = cur->cache_next;
                else fast_caches[i].free_list = cur->cache_next;
                fast_caches[i].count--;
                blk->in_cache = 0;
                blk->cache_next = NULL;
                return;
            }
            prev = cur;
            cur = cur->cache_next;
        }
    }
}

// Coalesce adjacent free blocks
static HeapBlock* CoalesceBlock(HeapBlock* block) {
    if (!block || !block->is_free) return block;

    // Coalesce with next block
    while (block->next && block->next->is_free && AreAdjacent(block, block->next)) {
        HeapBlock* next = block->next;
        CacheRemove(next);  // Remove from cache if present

        block->size += sizeof(HeapBlock) + next->size;
        block->next = next->next;
        if (next->next) next->next->prev = block;
        UpdateChecksum(block);
    }

    // Coalesce with previous block
    while (block->prev && block->prev->is_free && AreAdjacent(block->prev, block)) {
        HeapBlock* prev = block->prev;
        CacheRemove(prev);  // Remove from cache if present

        prev->size += sizeof(HeapBlock) + block->size;
        prev->next = block->next;
        if (block->next) block->next->prev = prev;
        UpdateChecksum(prev);
        block = prev;
    }

    return block;
}

// Flush cache and force coalescing (called periodically)
static void FlushCacheAndCoalesce(void) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        FastCache* cache = &fast_caches[i];
        while (cache->free_list) {
            HeapBlock* block = cache->free_list;
            cache->free_list = block->cache_next;
            cache->count--;
            
            block->in_cache = 0;
            block->cache_next = NULL;
            CoalesceBlock(block);
        }
    }
}


void KernelHeapInit() {
    kheap_lock = rust_spinlock_new();
    if (!kheap_lock) {
        PrintKernelError("Heap: Failed to allocate lock\n");
        return;
    }
    heap_head = NULL;
    total_allocated = 0;
    peak_allocated = 0;

    // Initialize enhanced fast caches
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        fast_caches[i].free_list = NULL;
        fast_caches[i].count = 0;
        fast_caches[i].hits = 0;
        fast_caches[i].misses = 0;
    }
    // change as needed
    KernelHeapSetValidationLevel(KHEAP_VALIDATION_BASIC);
}

void* KernelMemoryAlloc(size_t size) {
    if (size == 0 || size > MAX_ALLOC_SIZE) {
        return NULL;
    }

    size = AlignSize(size);
    if (size < MIN_BLOCK_SIZE) size = MIN_BLOCK_SIZE;
    
    // Periodic coalescing every 1000 allocations
    if ((++alloc_counter % 1000) == 0) {
        uint64_t flush_flags = rust_spinlock_lock_irqsave(kheap_lock);
        FlushCacheAndCoalesce();
        rust_spinlock_unlock_irqrestore(kheap_lock, flush_flags);
    }

    // Fast path for small allocations
    int size_class = GetSizeClass(size);
    if (size_class >= 0) {
        size_t actual_size = size_classes[size_class];

        uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

        // Try fast cache first
        HeapBlock* block = FastCachePop(size_class);
        if (block) {
            fast_caches[size_class].hits++;
            InitBlock(block, actual_size, 0);
            if (validation_level > 1) {
                FastMemset(BlockToUser(block), 0xAA, actual_size); // poison on alloc (debug)
            }
            total_allocated += actual_size;
            if (total_allocated > peak_allocated) {
                peak_allocated = total_allocated;
            }
            rust_spinlock_unlock_irqrestore(kheap_lock, flags);
            return BlockToUser(block);
        }
        fast_caches[size_class].misses++;

        rust_spinlock_unlock_irqrestore(kheap_lock, flags);
        size = actual_size; // Use size class size
    }

    // Standard allocation path
    uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

    HeapBlock* block = FindFreeBlock(size);
    if (block) {
        if (!ValidateBlock(block, "alloc_reuse")) {
            rust_spinlock_unlock_irqrestore(kheap_lock, flags);
            return NULL;
        }

        SplitBlock(block, size);
        InitBlock(block, size, 0);
        if (validation_level > 1) {
            FastMemset(BlockToUser(block), 0xAA, size);
        }
    } else {
        block = CreateNewBlock(size);
        if (!block) {
            rust_spinlock_unlock_irqrestore(kheap_lock, flags);
            return NULL;
        }
        if (validation_level > 1) {
            FastMemset(BlockToUser(block), 0xAA, size);
        }
    }

    total_allocated += size;
    if (total_allocated > peak_allocated) {
        peak_allocated = total_allocated;
    }

    rust_spinlock_unlock_irqrestore(kheap_lock, flags);
    return BlockToUser(block);
}

void* KernelAllocate(size_t num, size_t size) {
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
        PrintKernelError("[HEAP] Realloc of freed memory\n");
        return NULL;
    }

    size_t old_size = block->size;
    size_t aligned_size = AlignSize(size);

    if (aligned_size <= old_size) {
        return ptr; // Fits in current block
    }

    void* new_ptr = KernelMemoryAlloc(size);
    if (!new_ptr) return NULL;

    FastMemcpy(new_ptr, ptr, old_size);
    KernelFree(ptr);
    return new_ptr;
}

void KernelFree(void* ptr) {
    if (!ptr) return;

    HeapBlock* block = UserToBlock(ptr);
    if (!ValidateBlock(block, "free")) {
        return;
    }

    if (block->is_free) {
        PrintKernelError("[HEAP] Double free detected\n");
        return;
    }

    size_t size = block->size;

    // Fast path for small allocations
    int size_class = GetSizeClass(size);
    if (size_class >= 0 && size == size_classes[size_class]) {
        // Security: zero user data
        FastMemset(ptr, 0, size);

        uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

        InitBlock(block, size, 1);
        total_allocated -= size;

        // Try to cache the block
        FastCachePush(block, size_class);

        rust_spinlock_unlock_irqrestore(kheap_lock, flags);
        return;
    }

    // Standard free path
    uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

    // Security: zero user data
    FastMemset(ptr, 0, size);

    InitBlock(block, size, 1);
    total_allocated -= size;

    CoalesceBlock(block);

    rust_spinlock_unlock_irqrestore(kheap_lock, flags);
}

void PrintHeapStats(void) {
    const uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

    size_t free_blocks = 0, used_blocks = 0;
    size_t free_bytes = 0, used_bytes = 0;
    size_t cached_blocks = 0;
    size_t largest_free = 0;

    for (HeapBlock* block = heap_head; block; block = block->next) {
        if (!ValidateBlock(block, "stats")) continue;

        if (block->is_free) {
            free_blocks++;
            free_bytes += block->size;
            if (block->size > largest_free) largest_free = block->size;
        } else {
            used_blocks++;
            used_bytes += block->size;
        }
    }

    // Count cached blocks
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        cached_blocks += fast_caches[i].count;
    }

    rust_spinlock_unlock_irqrestore(kheap_lock, flags);

    PrintKernel("[HEAP] Blocks: "); PrintKernelInt(used_blocks);
    PrintKernel(", "); PrintKernelInt(free_blocks); PrintKernel(" free, ");
    PrintKernelInt(cached_blocks); PrintKernel(" cached\n");
    PrintKernel("[HEAP] Memory: "); PrintKernelInt(used_bytes / 1024);
    PrintKernel("KB used, "); PrintKernelInt(free_bytes / 1024); PrintKernel("KB free\n");
    PrintKernel("[HEAP] Peak: "); PrintKernelInt(peak_allocated / 1024); PrintKernel("KB\n");

    if (free_bytes > 0) {
        int frag = (int)(((free_bytes - largest_free) * 100) / free_bytes);
        PrintKernel("[HEAP] Fragmentation: "); PrintKernelInt(frag); PrintKernel("% (largest free block ");
        PrintKernelInt(largest_free); PrintKernel(" bytes)\n");
    }

    // Show cache efficiency
    PrintKernel("[HEAP] Cache stats:\n");
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (fast_caches[i].hits + fast_caches[i].misses > 0) {
            int hit_rate = (int)((fast_caches[i].hits * 100) / (fast_caches[i].hits + fast_caches[i].misses));
            PrintKernel("  "); PrintKernelInt(size_classes[i]); PrintKernel("B: ");
            PrintKernelInt(hit_rate); PrintKernel("% hit rate\n");
        }
    }
}

void KernelHeapSetValidationLevel(int level) {
    validation_level = (level < 0) ? 0 : (level > 2) ? 2 : level;
}

// Flush fast caches (useful for debugging or low-memory situations)
void KernelHeapFlushCaches(void) {
    uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        while (fast_caches[i].free_list) {
            HeapBlock* block = FastCachePop(i);
            if (block) {
                CoalesceBlock(block);
            }
        }
    }

    rust_spinlock_unlock_irqrestore(kheap_lock, flags);
}


void KernelHeapTune(size_t small_alloc_threshold, int fast_cache_capacity) {
    uint64_t flags = rust_spinlock_lock_irqsave(kheap_lock);

    // Clamp to sane bounds
    if (small_alloc_threshold < MIN_BLOCK_SIZE) small_alloc_threshold = MIN_BLOCK_SIZE;
    if (small_alloc_threshold > 8192) small_alloc_threshold = 8192; // cap to keep chunking reasonable
    if (fast_cache_capacity < 0) fast_cache_capacity = 0;
    if (fast_cache_capacity > 1024) fast_cache_capacity = 1024; // prevent runaway memory in caches

    g_small_alloc_threshold = AlignSize(small_alloc_threshold);
    g_fast_cache_capacity = fast_cache_capacity;

    // If capacity shrank, proactively trim caches and coalesce
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        while (fast_caches[i].count > g_fast_cache_capacity) {
            HeapBlock* blk = FastCachePop(i);
            if (blk) {
                // Mark free and merge back to main free space
                InitBlock(blk, size_classes[i], 1);
                CoalesceBlock(blk);
            }
        }
    }

    rust_spinlock_unlock_irqrestore(kheap_lock, flags);
}
