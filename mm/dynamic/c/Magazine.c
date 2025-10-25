#include "Magazine.h"
#include "MemOps.h"
#include "Console.h"
#include "../VMem.h"
#include "Panic.h"
#include "SpinlockRust.h"
#include "../drivers/APIC/APIC.h"
#include "../../arch/x86_64/features/x64.h"
#include "../../include/Io.h"

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
    FastMemset(mag, 0, sizeof(Magazine)); // Clear contents
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
        header->size = size;
        return (void*)(header + 1); // Return pointer to user data
    }

    // --- Small Allocation Fast Path ---
    uint32_t cpu_id = GetCpuId();
    PerCpuCache* cache = &per_cpu_caches[cpu_id];
    Magazine* mag = cache->active_magazines[sc_idx];

    // If the active magazine is not empty, pop a block and return it.
    if (mag && mag->count > 0) {
        mag->count--;
        return mag->blocks[mag->count];
    }

    // --- Slow Path: Refill from Depot ---
    uint64_t flags = rust_spinlock_lock_irqsave(depot.lock);
    mag = DepotRefill(sc_idx);
    rust_spinlock_unlock_irqrestore(depot.lock, flags);

    if (!mag) {
        PrintKernelError("Heap: Failed to refill magazine, out of memory.\n");
        return NULL;
    }

    // Install the new magazine and retry the allocation.
    cache->active_magazines[sc_idx] = mag;
    mag->count--;
    return mag->blocks[mag->count];
}

/**
 * @brief Frees a block of memory.
 */
void MagazineFree(void* ptr) {
    if (!ptr) {
        return;
    }

    // --- Determine Slab and Size Class (lockless via page-hash) ---
    Slab* slab = FindSlabForPointer(ptr);
    if (!slab) {
        // Assume it's a large allocation
        LargeBlockHeader* header = (LargeBlockHeader*)ptr - 1; // Get pointer to header
        // Basic sanity: header must be mapped; we trust VMem
        VMemFree((void*)header, header->size + sizeof(LargeBlockHeader));
        return;
    }

    // Bounds and alignment checks
    if (ptr < slab->base_ptr || ptr >= (uint8_t*)slab->base_ptr + SLAB_SIZE) {
        PrintKernelError("Heap: Freeing pointer outside of slab bounds!\n");
        return;
    }
    uintptr_t off = (uintptr_t)((uint8_t*)ptr - (uint8_t*)slab->base_ptr);
    if ((off % slab->block_size) != 0) {
        PrintKernelError("Heap: Free pointer not aligned to block size!\n");
        return;
    }

    int sc_idx = slab->size_class_index;

    // --- Small Allocation Fast Path (per-CPU, IRQ-disabled) ---
    irq_flags_t iflags = save_irq_flags();
    cli();
    uint32_t cpu_id = GetCpuId();
    PerCpuCache* cache = &per_cpu_caches[cpu_id];
    Magazine* mag = cache->active_magazines[sc_idx];

    if (mag && mag->count < MAGAZINE_CAPACITY) {
        mag->blocks[mag->count] = ptr;
        mag->count++;
        restore_irq_flags(iflags);
        return;
    }
    restore_irq_flags(iflags);

    // --- Slow Path: Return to Depot and/or install new magazine ---
    uint64_t flags = rust_spinlock_lock_irqsave(depot.lock);
    // Reload per-CPU magazine under lock in case another context changed it
    cpu_id = GetCpuId();
    cache = &per_cpu_caches[cpu_id];
    mag = cache->active_magazines[sc_idx];

    if (mag) {
        DepotReturn(mag, sc_idx);
    }

    Magazine* new_mag = AllocMagazine();
    if (!new_mag) {
        PANIC("Magazine pool exhausted during free operation!");
    }
    new_mag->blocks[0] = ptr;
    new_mag->count = 1;
    new_mag->next = NULL;
    cache->active_magazines[sc_idx] = new_mag;

    rust_spinlock_unlock_irqrestore(depot.lock, flags);
}

// =================================================================================================
// Depot Logic (Slow Path) - Implementation
// =================================================================================================

/**
 * @brief Finds the Slab that contains the given pointer.
 * This is a linear scan and can be slow. Needs optimization for production.
 * Assumes depot.lock is held by the caller.
 */
// Lightweight slab hash for O(1) pointer-to-slab lookup
#define SLAB_HASH_SIZE 4096

typedef struct SlabHashNode {
    uintptr_t page_key; // (addr >> PAGE_SHIFT)
    Slab* slab;
    struct SlabHashNode* next;
} SlabHashNode;

static SlabHashNode* slab_hash[SLAB_HASH_SIZE];

static inline uint32_t slab_hash_index(uintptr_t page_key) {
    return (uint32_t)(page_key & (SLAB_HASH_SIZE - 1));
}

static void SlabHashInsert(Slab* slab) {
    // Map each page in the slab region to this slab
    uintptr_t start = (uintptr_t)slab->base_ptr;
    for (uintptr_t addr = start; addr < start + SLAB_SIZE; addr += PAGE_SIZE) {
        uintptr_t key = addr >> PAGE_SHIFT;
        uint32_t idx = slab_hash_index(key);
        SlabHashNode* node = (SlabHashNode*)VMemAlloc(sizeof(SlabHashNode));
        if (!node) {
            PANIC("SlabHashInsert: OOM");
        }
        node->page_key = key;
        node->slab = slab;
        node->next = slab_hash[idx];
        slab_hash[idx] = node;
    }
}

static void SlabHashRemove(Slab* slab) {
    uintptr_t start = (uintptr_t)slab->base_ptr;
    for (uintptr_t addr = start; addr < start + SLAB_SIZE; addr += PAGE_SIZE) {
        uintptr_t key = addr >> PAGE_SHIFT;
        uint32_t idx = slab_hash_index(key);
        SlabHashNode** pp = &slab_hash[idx];
        while (*pp) {
            if ((*pp)->page_key == key) {
                SlabHashNode* dead = *pp;
                *pp = dead->next;
                VMemFree(dead, sizeof(SlabHashNode));
                break;
            }
            pp = &(*pp)->next;
        }
    }
}

static Slab* FindSlabForPointer(void* ptr) {
    uintptr_t key = ((uintptr_t)ptr) >> PAGE_SHIFT;
    uint32_t idx = slab_hash_index(key);
    SlabHashNode* node = slab_hash[idx];
    while (node) {
        if (node->page_key == key) {
            Slab* slab = node->slab;
            // Verify bounds
            if (ptr >= slab->base_ptr && ptr < (uint8_t*)slab->base_ptr + SLAB_SIZE) {
                return slab;
            }
            return NULL;
        }
        node = node->next;
    }
    return NULL;
}

/**
 * @brief Gets a new or partially full magazine from the depot.
 * This is the slow path for allocation.
 * Assumes depot.lock is held.
 */
static Magazine* DepotRefill(int size_class_index) {
    SizeClassDepot* sc_depot = &depot.size_class_depots[size_class_index];
    size_t block_size = size_classes[size_class_index];
    Magazine* mag = NULL;

    // 1. Prioritize getting a partial magazine
    if (sc_depot->partial_magazines) {
        mag = sc_depot->partial_magazines;
        sc_depot->partial_magazines = mag->next;
        mag->next = NULL;
        return mag;
    }

    // 2. If no partial magazines, try to get an empty magazine and fill it from a slab
    if (sc_depot->empty_magazines) {
        mag = sc_depot->empty_magazines;
        sc_depot->empty_magazines = mag->next;
        mag->next = NULL;
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

    // 3. If no existing slabs have free blocks, allocate a new slab
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
    FastMemset(mem, 0, SLAB_SIZE);

    new_slab->alloc_base = mem;
    new_slab->alloc_size = SLAB_SIZE;
    new_slab->base_ptr = mem; // Keep identical; SLAB_SIZE granularity not required
    new_slab->block_size = block_size;
    new_slab->size_class_index = size_class_index;
    new_slab->total_blocks = SLAB_SIZE / block_size;
    new_slab->free_blocks = new_slab->total_blocks;
    new_slab->cookie = rdtsc() ^ ((uintptr_t)new_slab);

    // Build free list within the new slab
    for (int i = 0; i < new_slab->total_blocks; i++) {
        void* block = (uint8_t*)new_slab->base_ptr + (i * block_size);
        *((void**)block) = new_slab->free_list_head; // Push to free list
        new_slab->free_list_head = block;
    }

    // Add new slab to depot's slab list
    new_slab->next = sc_depot->slabs;
    sc_depot->slabs = new_slab;
    // Insert into fast slab lookup hash
    SlabHashInsert(new_slab);

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

    // Store original count before returning blocks to slabs
    uint32_t original_mag_count = mag->count;

    // Return blocks from the magazine to their respective slabs.
    for (int i = 0; i < original_mag_count; i++) {
        void* block_ptr = mag->blocks[i];
        Slab* slab = FindSlabForPointer(block_ptr); // depot.lock is held by caller
        if (slab) {
            // Basic check: ensure the pointer is within the slab's bounds
            if (block_ptr < slab->base_ptr || block_ptr >= (uint8_t*)slab->base_ptr + SLAB_SIZE) {
                PrintKernelError("Heap: DepotReturn: Freeing pointer outside of slab bounds!\n");
                continue; // Skip this block
            }

            // Push block back to slab's free list
            *((void**)block_ptr) = slab->free_list_head;
            slab->free_list_head = block_ptr;
            slab->free_blocks++;

            // Check if slab is now completely empty and can be freed
            if (slab->free_blocks == slab->total_blocks) {
                // Remove slab from sc_depot->slabs list
                Slab* prev_slab = NULL;
                Slab* current_slab = sc_depot->slabs;
                while (current_slab && current_slab != slab) {
                    prev_slab = current_slab;
                    current_slab = current_slab->next;
                }

                if (current_slab == slab) {
                    if (prev_slab) {
                        prev_slab->next = slab->next;
                    }
                    else {
                        sc_depot->slabs = slab->next;
                    }
                    // Free the slab's memory and metadata
                    SlabHashRemove(slab);
                    VMemFree(slab->alloc_base, slab->alloc_size);
                    VMemFree(slab, sizeof(Slab));
                }
            }
        }
    }
    mag->count = 0; // Magazine is now empty

    // Categorize the magazine and add to appropriate list based on its original state
    if (original_mag_count == MAGAZINE_CAPACITY) {
        mag->next = sc_depot->full_magazines;
        sc_depot->full_magazines = mag;
    } else if (original_mag_count > 0) {
        mag->next = sc_depot->partial_magazines;
        sc_depot->partial_magazines = mag;
    } else { // Empty: return to magazine pool
        FreeMagazine(mag);
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

    // Get original size from header for large allocations, or from slab for small.
    size_t old_size = 0;
    Slab* slab = FindSlabForPointer(ptr);
    if (slab) {
        old_size = slab->block_size; // For small allocations, size is block_size
    } else {
        // Assume large allocation
        LargeBlockHeader* header = (LargeBlockHeader*)ptr - 1;
        old_size = header->size;
    }

    void* new_ptr = MagazineAlloc(size);
    if (!new_ptr) {
        return NULL;
    }
    FastMemcpy(new_ptr, ptr, (size < old_size) ? size : old_size);
    MagazineFree(ptr);
    return new_ptr;
}