#include "Magazine.h"
#include "MemOps.h"
#include "Console.h"
#include "VMem.h"
#include "Panic.h"
#include "SpinlockRust.h"
#include "drivers/APIC/APIC.h"
#include "arch/x86_64/features/x64.h"
#include "include/Io.h"
#include "mm/KernelHeap.h"
#ifndef KHEAP_VALIDATION_NONE
#define KHEAP_VALIDATION_NONE 0
#define KHEAP_VALIDATION_BASIC 1
#define KHEAP_VALIDATION_FULL 2
#endif

// =================================================================================================
// Statistics (lightweight, best-effort)
// =================================================================================================

typedef struct {
    uint64_t alloc_fast_hits[NUM_SIZE_CLASSES];
    uint64_t alloc_slow_refills[NUM_SIZE_CLASSES];
    uint64_t free_fast_hits[NUM_SIZE_CLASSES];
    uint64_t free_slow_paths[NUM_SIZE_CLASSES];
    uint64_t magazine_swaps[NUM_SIZE_CLASSES];
    uint64_t slabs_allocated[NUM_SIZE_CLASSES];
} HeapStatsPerCpu;

static __attribute__((aligned(64))) HeapStatsPerCpu heap_stats_per_cpu[MAX_CPU_CORES];

// Runtime knobs
static volatile int g_validation_level = KHEAP_VALIDATION_NONE; // 0=none,1=basic,2=full
static volatile int g_stats_enabled = 1; // can be toggled by perf mode

static inline void StatsAdd(uint32_t cpu, int sc, volatile uint64_t* ctr) {
    // Best-effort increment; tearing is acceptable for stats.
    if (!g_stats_enabled) return;
    (void)cpu; (void)sc;
    (*((uint64_t*)ctr))++;
}

static void StatsSlabAllocated(int sc) {
    for (;;) {
        // Use CPU id defensively; OK if called under depot lock from any CPU.
        uint32_t cpu = lapic_get_id();
        heap_stats_per_cpu[cpu].slabs_allocated[sc]++;
        return;
    }
}

// =================================================================================================
// Global Variables
// =================================================================================================

// The single, global depot.
Depot depot;

// The array of per-CPU caches. This is aligned to 64 bytes to prevent false sharing.
__attribute__((aligned(64))) PerCpuCache per_cpu_caches[MAX_CPU_CORES];

// A simple pool for Magazine structures to avoid VMemAlloc for every magazine.
#define MAGAZINE_POOL_SIZE (MAX_CPU_CORES * NUM_SIZE_CLASSES * 2) // Enough for active + some depot
static Magazine magazine_pool[MAGAZINE_POOL_SIZE];
static Magazine* magazine_pool_head = NULL;

// =================================================================================================
// Forward Declarations for Depot Logic (Slow Path)
// =================================================================================================

static Magazine* DepotRefill(int size_class_index);
static void DepotReturn(Magazine* mag, int size_class_index);
static Slab* FindSlabForPointer(void* ptr);
#ifdef VF_CONFIG_HEAP_HYBRID
#include "mm/dynamic/rust/KernelHeapRust.h"
#include "drivers/APIC/APIC.h"
#endif

// =================================================================================================
// Helper Functions
// =================================================================================================

/**
 * @brief Gets the current CPU's ID.
 * Assumes LAPIC is initialized and available.
 */
static inline uint32_t GetCpuId(void) {
    // In a real scenario, this might need to be more robust, but for now, lapic_get_id is fine.
    return lapic_get_id();
}

/**
 * @brief Determines the appropriate size class index for a given allocation size.
 * @return The index into the size_classes array, or -1 if the size is too large.
 */
static inline int GetSizeClass(size_t size) {
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= size_classes[i]) {
            return i;
        }
    }
    return -1; // Indicates a large allocation
}

/**
 * @brief Allocates a Magazine structure from the internal pool.
 */
static Magazine* AllocMagazine(void) {
    if (magazine_pool_head == NULL) {
        return NULL; // Pool exhausted
    }
    Magazine* mag = magazine_pool_head;
    magazine_pool_head = magazine_pool_head->next;
    // Avoid full memset for speed; only initialize fields we rely on
    mag->count = 0;
    mag->next = NULL;
    return mag;
}

/**
 * @brief Frees a Magazine structure back to the internal pool.
 */
static void FreeMagazine(Magazine* mag) {
    if (mag == NULL) return;
    mag->next = magazine_pool_head;
    magazine_pool_head = mag;
}

// =================================================================================================
// Public API Implementation
// =================================================================================================

/**
 * @brief Initializes the magazine heap allocator.
 */
void MagazineInit() {
    // Initialize the depot lock
    depot.lock = rust_spinlock_new();
    if (!depot.lock) {
        PANIC("Failed to initialize depot lock for magazine allocator");
    }


    // Initialize all depot and cache structures to zero.
    FastMemset(&depot.size_class_depots, 0, sizeof(depot.size_class_depots));
    FastMemset(&per_cpu_caches, 0, sizeof(per_cpu_caches));

    // Initialize magazine pool
    magazine_pool_head = &magazine_pool[0];
    for (int i = 0; i < MAGAZINE_POOL_SIZE - 1; i++) {
        magazine_pool[i].next = &magazine_pool[i+1];
    }
    magazine_pool[MAGAZINE_POOL_SIZE - 1].next = NULL;

    PrintKernelSuccess("System: Magazine heap allocator initialized\n");
}

/**
 * @brief Allocates a block of memory.
 */
void* MagazineAlloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    int sc_idx = GetSizeClass(size);

    // --- Large Allocation Fallback ---
    if (sc_idx < 0) {
        // Allocate space for the header + requested size
        size_t total_size = size + sizeof(LargeBlockHeader);
        void* raw_mem = VMemAlloc(total_size);
        if (!raw_mem) {
            return NULL;
        }
        LargeBlockHeader* header = (LargeBlockHeader*)raw_mem;
        header->magic = LARGE_BLOCK_MAGIC;
        header->size = size;
        void* user_ptr = (void*)(header + 1);
        if (g_validation_level == KHEAP_VALIDATION_FULL) {
            FastMemset(user_ptr, 0xCD, size);
        }
        return user_ptr; // Return pointer to user data
    }

    // --- Small Allocation Fast Path ---
    uint32_t cpu_id = GetCpuId();
    PerCpuCache* cache = &per_cpu_caches[cpu_id];
    Magazine* mag = cache->active_magazines[sc_idx];

    // If the active magazine is not empty, pop a block and return it.
    if (mag && mag->count > 0) {
        mag->count--;
        StatsAdd(cpu_id, sc_idx, &heap_stats_per_cpu[cpu_id].alloc_fast_hits[sc_idx]);

        void* raw_block = mag->blocks[mag->count];
        MagazineBlockHeader* header = (MagazineBlockHeader*)raw_block;
        header->magic = MAGAZINE_BLOCK_MAGIC;
        header->sc_idx = sc_idx;

        void* user_ptr = (void*)(header + 1);

        if (g_validation_level == KHEAP_VALIDATION_FULL) {
            FastMemset(user_ptr, 0xCD, size_classes[sc_idx]);
        }
        return user_ptr;
    }

    // --- Slow Path: Refill from Depot ---
    uint64_t flags = rust_spinlock_lock_irqsave(depot.lock);
    mag = DepotRefill(sc_idx);
    rust_spinlock_unlock_irqrestore(depot.lock, flags);

    if (mag) {
        // stats: slow-path refill and magazine swap
        StatsAdd(cpu_id, sc_idx, &heap_stats_per_cpu[cpu_id].alloc_slow_refills[sc_idx]);
        StatsAdd(cpu_id, sc_idx, &heap_stats_per_cpu[cpu_id].magazine_swaps[sc_idx]);
    }

    if (!mag) {
        PrintKernelError("Heap: Failed to refill magazine, out of memory.\n");
        return NULL;
    }

    // Install the new magazine and retry the allocation.
    cache->active_magazines[sc_idx] = mag;
    mag->count--;

    void* raw_block = mag->blocks[mag->count];
    MagazineBlockHeader* header = (MagazineBlockHeader*)raw_block;
    header->magic = MAGAZINE_BLOCK_MAGIC;
    header->sc_idx = sc_idx;

    void* user_ptr = (void*)(header + 1);

    if (g_validation_level == KHEAP_VALIDATION_FULL) {
        FastMemset(user_ptr, 0xCD, size_classes[sc_idx]);
    }
    return user_ptr;
}

/**
 * @brief Frees a block of memory.
 */
static inline void PoisonOnFreeSmall(void* ptr, size_t size) {
    if (g_validation_level == KHEAP_VALIDATION_NONE) return;
    size_t sz = size;
    if (g_validation_level == KHEAP_VALIDATION_BASIC) {
        // Poison a small header and trailer region if possible
        size_t n = sz < 32 ? sz : 32;
        FastMemset(ptr, 0xDD, n);
    } else { // FULL
        FastMemset(ptr, 0xDD, sz);
    }
}

void MagazineFree(void* ptr) {
    if (!ptr) {
        return;
    }

    // Check for small allocation magic number from our header
    MagazineBlockHeader* header = (MagazineBlockHeader*)ptr - 1;

    if (header && header->magic == MAGAZINE_BLOCK_MAGIC) {
        // --- Small Allocation Fast Path ---
        int sc_idx = header->sc_idx;
        void* raw_block = (void*)header;

        irq_flags_t iflags = save_irq_flags();
        cli();
        uint32_t cpu_id = GetCpuId();
        PerCpuCache* cache = &per_cpu_caches[cpu_id];
        Magazine* mag = cache->active_magazines[sc_idx];

        if (mag && mag->count < MAGAZINE_CAPACITY) {
            PoisonOnFreeSmall(ptr, size_classes[sc_idx]);
            mag->blocks[mag->count] = raw_block;
            mag->count++;
            StatsAdd(cpu_id, sc_idx, &heap_stats_per_cpu[cpu_id].free_fast_hits[sc_idx]);
            restore_irq_flags(iflags);
            return;
        }
        restore_irq_flags(iflags);

        // --- Slow Path: Return to Depot ---
        uint64_t flags = rust_spinlock_lock_irqsave(depot.lock);
        cpu_id = GetCpuId(); // Reload per-CPU magazine under lock
        cache = &per_cpu_caches[cpu_id];
        mag = cache->active_magazines[sc_idx];

        if (mag) {
            DepotReturn(mag, sc_idx);
        }

        Magazine* new_mag = AllocMagazine();
        if (!new_mag) {
            PANIC("Magazine pool exhausted during free operation!");
        }
        PoisonOnFreeSmall(ptr, size_classes[sc_idx]);
        new_mag->blocks[0] = raw_block;
        new_mag->count = 1;
        new_mag->next = NULL;
        cache->active_magazines[sc_idx] = new_mag;

        StatsAdd(cpu_id, sc_idx, &heap_stats_per_cpu[cpu_id].free_slow_paths[sc_idx]);
        StatsAdd(cpu_id, sc_idx, &heap_stats_per_cpu[cpu_id].magazine_swaps[sc_idx]);

        rust_spinlock_unlock_irqrestore(depot.lock, flags);
        return;
    }

    // --- Large or Foreign Allocation ---
    LargeBlockHeader* large_header = (LargeBlockHeader*)ptr - 1;
    if (large_header && large_header->magic == LARGE_BLOCK_MAGIC) {
        if (g_validation_level != KHEAP_VALIDATION_NONE) {
            FastMemset(ptr, 0xDD, large_header->size);
        }
        VMemFree((void*)large_header, large_header->size + sizeof(LargeBlockHeader));
        return;
    }

#ifdef VF_CONFIG_HEAP_HYBRID
    // Delegate to Rust heap for unknown blocks
    rust_kfree(ptr);
    return;
#else
    PANIC("MagazineFree: unknown pointer freed");
#endif
}

// =================================================================================================
// Depot Logic (Slow Path) - Implementation
// =================================================================================================



/**
 * @brief Gets a new or partially full magazine from the depot.
 * This is the slow path for allocation.
 * Assumes depot.lock is held.
 */
static Magazine* DepotRefill(int size_class_index) {
    SizeClassDepot* sc_depot = &depot.size_class_depots[size_class_index];
    Magazine* mag = NULL;

    // 1. Prefer a full magazine for maximum fast-path allocations
    if (sc_depot->full_magazines) {
        mag = sc_depot->full_magazines;
        sc_depot->full_magazines = mag->next;
        mag->next = NULL;
        return mag;
    }

    // 2. Next, try a partial magazine
    if (sc_depot->partial_magazines) {
        mag = sc_depot->partial_magazines;
        sc_depot->partial_magazines = mag->next;
        mag->next = NULL;
        return mag;
    }

    // 3. If no magazines with content, get an empty one (or create) and fill from slabs
    if (sc_depot->empty_magazines) {
        mag = sc_depot->empty_magazines;
        sc_depot->empty_magazines = mag->next;
        mag->next = NULL;
        mag->count = 0;
    } else {
        // If no empty magazines in depot, allocate a new one from the pool
        mag = AllocMagazine();
        if (!mag) {
            return NULL; // Magazine pool exhausted
        }
    }

    // Now, fill the magazine from an existing slab with free blocks
    Slab* current_slab = sc_depot->slabs;
    while (current_slab) {
        if (current_slab->free_blocks > 0) {
            // Fill magazine from this slab's free list
            for (int i = 0; i < MAGAZINE_CAPACITY && current_slab->free_blocks > 0; i++) {
                void* block = current_slab->free_list_head;
                if (block) {
                    current_slab->free_list_head = *((void**)block); // Pop from free list
                    mag->blocks[mag->count++] = block;
                    current_slab->free_blocks--;
                }
            }
            return mag;
        }
        current_slab = current_slab->next;
    }

    // 4. If no existing slabs have free blocks, allocate a new slab
    Slab* new_slab = VMemAlloc(sizeof(Slab)); // Allocate Slab metadata
    if (!new_slab) {
        FreeMagazine(mag); // Free the magazine we just allocated
        return NULL;
    }
    FastMemset(new_slab, 0, sizeof(Slab));

    void* mem = VMemAlloc(SLAB_SIZE); // Allocate slab memory
    if (!mem) {
        VMemFree(new_slab, sizeof(Slab));
        FreeMagazine(mag);
        return NULL;
    }
    // Avoid zeroing the entire slab for speed; blocks are uninitialized by design.

    size_t chunk_size = sizeof(MagazineBlockHeader) + size_classes[size_class_index];

    new_slab->alloc_base = mem;
    new_slab->alloc_size = SLAB_SIZE;
    new_slab->base_ptr = mem; // Keep identical; SLAB_SIZE granularity not required
    new_slab->block_size = chunk_size;
    new_slab->size_class_index = size_class_index;
    new_slab->total_blocks = SLAB_SIZE / chunk_size;
    new_slab->free_blocks = new_slab->total_blocks;
    new_slab->cookie = rdtsc() ^ ((uintptr_t)new_slab);

    // Build free list within the new slab
    for (int i = 0; i < new_slab->total_blocks; i++) {
        void* block = (uint8_t*)new_slab->base_ptr + (i * chunk_size);
        *((void**)block) = new_slab->free_list_head; // Push to free list
        new_slab->free_list_head = block;
    }

    // Add new slab to depot's slab list
    new_slab->next = sc_depot->slabs;
    sc_depot->slabs = new_slab;
    // Stats: track slab allocation
    StatsSlabAllocated(size_class_index);

    // Fill the magazine from the new slab
    for (int i = 0; i < MAGAZINE_CAPACITY && new_slab->free_blocks > 0; i++) {
        void* block = new_slab->free_list_head;
        if (block) {
            new_slab->free_list_head = *((void**)block); // Pop from free list
            mag->blocks[mag->count++] = block;
            new_slab->free_blocks--;
        }
    }

    return mag;
}

/**
 * @brief Returns a full magazine to the depot.
 * This is the slow path for deallocation.
 * Assumes depot.lock is held.
 */
static void DepotReturn(Magazine* mag, int size_class_index) {
    SizeClassDepot* sc_depot = &depot.size_class_depots[size_class_index];

    // Do not spill blocks back to slabs; keep magazines intact for speed.
    // Simply classify the magazine based on its current fill level.
    if (mag->count >= MAGAZINE_CAPACITY) {
        mag->count = MAGAZINE_CAPACITY;
        mag->next = sc_depot->full_magazines;
        sc_depot->full_magazines = mag;
    } else if (mag->count > 0) {
        mag->next = sc_depot->partial_magazines;
        sc_depot->partial_magazines = mag;
    } else {
        // Empty magazine: keep it in the depot's empty list for reuse
        mag->next = sc_depot->empty_magazines;
        sc_depot->empty_magazines = mag;
    }
}


// =================================================================================================
// Other Public API Functions (Stubs or Simple Implementations)
// =================================================================================================

void* MagazineAllocate(size_t num, size_t size) {
    if (num > 0 && size > (UINT64_MAX / num)) {
        return NULL; // Overflow
    }
    size_t total_size = num * size;
    void* ptr = MagazineAlloc(total_size);
    if (ptr) {
        FastMemset(ptr, 0, total_size);
    }
    return ptr;
}

void* MagazineReallocate(void* ptr, size_t size) {
    if (!ptr) {
        return MagazineAlloc(size);
    }
    if (size == 0) {
        MagazineFree(ptr);
        return NULL;
    }

    // Get original size from header for large allocations, or from our new header for small.
    size_t old_size = 0;
    MagazineBlockHeader* small_header = (MagazineBlockHeader*)ptr - 1;

    if (small_header && small_header->magic == MAGAZINE_BLOCK_MAGIC) {
        old_size = size_classes[small_header->sc_idx];
    } else {
        LargeBlockHeader* large_header = (LargeBlockHeader*)ptr - 1;
        if (large_header && large_header->magic == LARGE_BLOCK_MAGIC) {
            old_size = large_header->size;
        } else {
            // In a hybrid system, realloc should be handled by a dispatcher
            // that knows which allocator owns the pointer.
            // Since we can't know the size, we can't safely reallocate.
            PANIC("MagazineReallocate: unknown pointer type");
        }
    }

    void* new_ptr = MagazineAlloc(size);
    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = (size < old_size) ? size : old_size;
    FastMemcpy(new_ptr, ptr, copy_size);
    MagazineFree(ptr);
    return new_ptr;
}

// =================================================================================================
// Stats printing
// =================================================================================================
static void PrintOneStatLine(int sc_idx, size_t block_sz, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e, uint64_t f) {
    PrintKernelF("SC[%d] sz=%zu | alloc_fast=%llu alloc_slow=%llu free_fast=%llu free_slow=%llu swaps=%llu slabs=%llu\n",
                 sc_idx, block_sz, a, b, c, d, e, f);
}

void MagazineFlushCaches(void) {
    uint64_t flags = rust_spinlock_lock_irqsave(depot.lock);
    // Return all per-CPU active magazines to the depot lists and clear them
    for (int cpu = 0; cpu < MAX_CPU_CORES; cpu++) {
        PerCpuCache* cache = &per_cpu_caches[cpu];
        for (int sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
            Magazine* mag = cache->active_magazines[sc];
            if (mag) {
                DepotReturn(mag, sc);
                cache->active_magazines[sc] = NULL;
            }
        }
    }
    rust_spinlock_unlock_irqrestore(depot.lock, flags);
}

void MagazineSetValidationLevel(int level) {
    if (level < KHEAP_VALIDATION_NONE) level = KHEAP_VALIDATION_NONE;
    if (level > KHEAP_VALIDATION_FULL) level = KHEAP_VALIDATION_FULL;
    g_validation_level = level;
}

void MagazineSetPerfMode(int mode) {
    // mode==0: disable stats; nonzero: enable
    g_stats_enabled = (mode != 0);
}

void MagazinePrintStats(void) {
    PrintKernel("\n[Heap] Magazine allocator statistics\n");
    uint64_t totals[6] = {0,0,0,0,0,0};
    for (int sc = 0; sc < NUM_SIZE_CLASSES; sc++) {
        uint64_t a=0,b=0,c=0,d=0,e=0,f=0;
        for (int cpu = 0; cpu < MAX_CPU_CORES; cpu++) {
            a += heap_stats_per_cpu[cpu].alloc_fast_hits[sc];
            b += heap_stats_per_cpu[cpu].alloc_slow_refills[sc];
            c += heap_stats_per_cpu[cpu].free_fast_hits[sc];
            d += heap_stats_per_cpu[cpu].free_slow_paths[sc];
            e += heap_stats_per_cpu[cpu].magazine_swaps[sc];
            f += heap_stats_per_cpu[cpu].slabs_allocated[sc];
        }
        totals[0]+=a; totals[1]+=b; totals[2]+=c; totals[3]+=d; totals[4]+=e; totals[5]+=f;
        PrintOneStatLine(sc, size_classes[sc], a,b,c,d,e,f);
    }
    PrintKernel("-----------------------------------------------------------\n");
    PrintKernelF("TOTAL           | alloc_fast=%llu alloc_slow=%llu free_fast=%llu free_slow=%llu swaps=%llu slabs=%llu\n",
                 totals[0], totals[1], totals[2], totals[3], totals[4], totals[5]);
}
