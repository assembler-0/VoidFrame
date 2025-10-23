#include "VMem.h"
#include "Atomics.h"
#include "Console.h"
#include "MemOps.h"
#include "PMem.h"
#include "Panic.h"
#include "Spinlock.h"

// Global variable for dynamic identity mapping size
uint64_t g_identity_map_size = 4ULL * 1024 * 1024 * 1024; // Default to 4GB, will be updated during init

static VirtAddrSpace kernel_space;
static volatile int vmem_lock = 0;
static uint64_t vmem_allocations = 0;
static uint64_t vmem_frees = 0;
static uint64_t tlb_flushes = 0;

// Buddy allocator constants
#define BUDDY_MIN_ORDER 12  // 4KB pages
#define BUDDY_MAX_ORDER 30  // 1GB max allocation
#define BUDDY_NUM_ORDERS (BUDDY_MAX_ORDER - BUDDY_MIN_ORDER + 1)

// Buddy free lists - one per order
static VMemFreeBlock* buddy_free_lists[BUDDY_NUM_ORDERS];
static uint64_t buddy_bitmap[(1ULL << (BUDDY_MAX_ORDER - BUDDY_MIN_ORDER)) / 64];

// Pre-allocated pool for buddy nodes
#define MAX_BUDDY_NODES 2048
static VMemFreeBlock buddy_node_pool[MAX_BUDDY_NODES];
static VMemFreeBlock* buddy_node_head = NULL;

// Hash table for fast buddy lookup
#define HASH_TABLE_SIZE 4096 // Must be a power of 2
static VMemFreeBlock* buddy_hash_table[HASH_TABLE_SIZE];

static inline uint32_t HashAddress(uint64_t addr) {
    // Multiplicative hashing for better distribution
    return (uint32_t)(((addr >> BUDDY_MIN_ORDER) * 2654435761) & (HASH_TABLE_SIZE - 1));
}

// TLB flush batching for efficiency
#define MAX_TLB_BATCH 64
static uint64_t tlb_batch[MAX_TLB_BATCH];
static uint32_t tlb_batch_count = 0;

// Identity-mapped page table cache for better allocation
#define PT_CACHE_SIZE 16
static void* pt_cache[PT_CACHE_SIZE];
static uint32_t pt_cache_count = 0;

// Buddy allocator regions
static uint64_t buddy_region_start[2];
static uint64_t buddy_region_size[2];

extern uint64_t total_pages;
extern uint8_t _kernel_phys_start[];
extern uint8_t _kernel_phys_end[];
extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _rodata_start[];
extern uint8_t _rodata_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t _bss_start[];
extern uint8_t _bss_end[];

static void InitBuddyNodePool(void) {
    buddy_node_head = &buddy_node_pool[0];
    for (int i = 0; i < MAX_BUDDY_NODES - 1; ++i) {
        buddy_node_pool[i].next = &buddy_node_pool[i + 1];
    }
    buddy_node_pool[MAX_BUDDY_NODES - 1].next = NULL;
}

static VMemFreeBlock* AllocBuddyNode(void) {
    if (!buddy_node_head) return NULL;
    VMemFreeBlock* node = buddy_node_head;
    buddy_node_head = node->next;
    return node;
}

static void ReleaseBuddyNode(VMemFreeBlock* node) {
    node->next = buddy_node_head;
    buddy_node_head = node;
}

static inline uint32_t GetOrder(uint64_t size) {
    if (size <= PAGE_SIZE) return 0;
    return 64 - __builtin_clzll(size - 1) - BUDDY_MIN_ORDER;
}

static inline uint64_t OrderToSize(uint32_t order) {
    return 1ULL << (order + BUDDY_MIN_ORDER);
}

static inline uint64_t GetBuddyAddr(uint64_t addr, uint32_t order) {
    return addr ^ OrderToSize(order);
}

static void BuddyRemoveFreeBlock(VMemFreeBlock* node, uint32_t order);

static void BuddyAddFreeBlock(uint64_t addr, uint32_t order) {
    if (order >= BUDDY_NUM_ORDERS) return;

    VMemFreeBlock* node = AllocBuddyNode();
    if (!node) return;

    node->base = addr;
    node->size = OrderToSize(order);
    node->prev = NULL;
    node->hnext = NULL;

    // Add to the head of the free list for this order
    node->next = buddy_free_lists[order];
    if (buddy_free_lists[order]) {
        buddy_free_lists[order]->prev = node;
    }
    buddy_free_lists[order] = node;

    // Add to the hash table
    uint32_t hash = HashAddress(addr);
    node->hnext = buddy_hash_table[hash];
    buddy_hash_table[hash] = node;
}

static VMemFreeBlock* BuddyFindFreeBlock(uint64_t addr, uint32_t order) {
    uint32_t hash = HashAddress(addr);
    VMemFreeBlock* curr = buddy_hash_table[hash];
    uint64_t size = OrderToSize(order);

    while (curr) {
        if (curr->base == addr && curr->size == size) {
            return curr;
        }
        curr = curr->hnext;
    }
    return NULL;
}

static void BuddyRemoveFreeBlock(VMemFreeBlock* node, uint32_t order) {
    // Remove from the doubly-linked free list
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        buddy_free_lists[order] = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }

    // Remove from the hash table
    uint32_t hash = HashAddress(node->base);
    VMemFreeBlock* prev_h = NULL;
    VMemFreeBlock* curr_h = buddy_hash_table[hash];
    while (curr_h) {
        if (curr_h == node) {
            if (prev_h) {
                prev_h->hnext = curr_h->hnext;
            } else {
                buddy_hash_table[hash] = curr_h->hnext;
            }
            break;
        }
        prev_h = curr_h;
        curr_h = curr_h->hnext;
    }

    // The node is now unlinked, release it back to the pool
    ReleaseBuddyNode(node);
}

static uint64_t BuddyAlloc(uint64_t size) {
    uint32_t order = GetOrder(size);
    if (order >= BUDDY_NUM_ORDERS) return 0;

    // Find smallest available block
    for (uint32_t curr_order = order; curr_order < BUDDY_NUM_ORDERS; curr_order++) {
        VMemFreeBlock* block = buddy_free_lists[curr_order];
        if (!block) continue;

        // Unlink the block from the head of the list
        if (block->next) {
            block->next->prev = NULL;
        }
        buddy_free_lists[curr_order] = block->next;

        // Remove from hash table
        uint32_t hash = HashAddress(block->base);
        VMemFreeBlock* prev_h = NULL;
        VMemFreeBlock* curr_h = buddy_hash_table[hash];
        while (curr_h) {
            if (curr_h == block) {
                if (prev_h) {
                    prev_h->hnext = curr_h->hnext;
                } else {
                    buddy_hash_table[hash] = curr_h->hnext;
                }
                break;
            }
            prev_h = curr_h;
            curr_h = curr_h->hnext;
        }

        uint64_t addr = block->base;
        ReleaseBuddyNode(block);

        // Split down to required order
        while (curr_order > order) {
            curr_order--;
            uint64_t buddy_addr = addr + OrderToSize(curr_order);
            BuddyAddFreeBlock(buddy_addr, curr_order);
        }

        return addr;
    }

    return 0; // No free blocks
}

static void BuddyFree(uint64_t addr, uint64_t size) {
    uint32_t order = GetOrder(size);
    if (order >= BUDDY_NUM_ORDERS) return;

    // Try to coalesce with buddy
    while (order < BUDDY_NUM_ORDERS - 1) {
        uint64_t buddy_addr = GetBuddyAddr(addr, order);
        VMemFreeBlock* buddy = BuddyFindFreeBlock(buddy_addr, order);

        if (!buddy) break; // Buddy not free

        // Buddy is free, so remove it from the data structures and coalesce
        BuddyRemoveFreeBlock(buddy, order);

        if (buddy_addr < addr) addr = buddy_addr;
        order++;
    }

    BuddyAddFreeBlock(addr, order);
}

static inline int IsValidPhysAddr(uint64_t paddr) {
    return (paddr != 0 && paddr < (total_pages * PAGE_SIZE));
}

static inline int IsValidVirtAddr(uint64_t vaddr) {
    // Check canonical address ranges
    return ((vaddr >= VIRT_ADDR_SPACE_LOW_START && vaddr <= VIRT_ADDR_SPACE_LOW_END) ||
            (vaddr >= VIRT_ADDR_SPACE_HIGH_START && vaddr <= VIRT_ADDR_SPACE_HIGH_END) ||
            (vaddr >= KERNEL_SPACE_START && vaddr <= KERNEL_SPACE_END));
}

static inline uint64_t* GetTableVirt(uint64_t phys_addr) {
    return (phys_addr < IDENTITY_MAP_SIZE) ?
           (uint64_t*)phys_addr : (uint64_t*)PHYS_TO_VIRT(phys_addr);
}

static void flush_tlb_batch(void) {
    if (tlb_batch_count == 0) return;

    if (tlb_batch_count > 8) {
        VMemFlushTLB();
    } else {
        for (uint32_t i = 0; i < tlb_batch_count; i++) {
            __asm__ volatile("invlpg (%0)" :: "r"(tlb_batch[i]) : "memory");
        }
    }
    tlb_batch_count = 0;
    tlb_flushes++;
}

static void add_to_tlb_batch(uint64_t vaddr) {
    if (tlb_batch_count >= MAX_TLB_BATCH) {
        flush_tlb_batch();
    }
    tlb_batch[tlb_batch_count++] = vaddr;
}

static void* alloc_identity_page_table(void) {
    if (pt_cache_count > 0) {
        return pt_cache[--pt_cache_count];
    }

    for (uint32_t attempt = 0; attempt < 32; attempt++) {
        void* candidate = AllocPage();
        if (!candidate) break;
        if ((uint64_t)candidate < IDENTITY_MAP_SIZE) {
            FastZeroPage(candidate);
            return candidate;
        }
        FreePage(candidate);
    }
    return NULL;
}

static void cache_page_table(void* pt) {
    if (pt_cache_count < PT_CACHE_SIZE && (uint64_t)pt < IDENTITY_MAP_SIZE) {
        pt_cache[pt_cache_count++] = pt;
    } else {
        FreePage(pt);
    }
}

void VMemInit(void) {
    InitBuddyNodePool();

    // Initialize buddy allocator
    for (int i = 0; i < BUDDY_NUM_ORDERS; i++) {
        buddy_free_lists[i] = NULL;
    }

    // Initialize hash table
    for (int i = 0; i < HASH_TABLE_SIZE; i++) {
        buddy_hash_table[i] = NULL;
    }

    // Set up buddy regions
    buddy_region_start[0] = VIRT_ADDR_SPACE_LOW_START;
    buddy_region_size[0] = VIRT_ADDR_SPACE_LOW_END - VIRT_ADDR_SPACE_LOW_START + 1;
    buddy_region_start[1] = VIRT_ADDR_SPACE_HIGH_START;
    buddy_region_size[1] = VIRT_ADDR_SPACE_HIGH_END - VIRT_ADDR_SPACE_HIGH_START + 1;

    // Add initial free blocks (largest possible)
    for (int region = 0; region < 2; region++) {
        uint64_t addr = buddy_region_start[region];
        uint64_t remaining = buddy_region_size[region];

        while (remaining >= PAGE_SIZE) {
            uint32_t order = BUDDY_NUM_ORDERS - 1;
            while (order > 0 && OrderToSize(order) > remaining) {
                order--;
            }

            BuddyAddFreeBlock(addr, order);
            uint64_t block_size = OrderToSize(order);
            addr += block_size;
            remaining -= block_size;
        }
    }

    // Get current PML4 from CR3 (set by bootstrap)
    uint64_t pml4_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(pml4_phys_addr));
    pml4_phys_addr &= ~0xFFF;

    kernel_space.pml4 = (uint64_t*)pml4_phys_addr;
    kernel_space.used_pages = 0;
    kernel_space.total_mapped = IDENTITY_MAP_SIZE;

    // Test identity mapping
    if (VMemGetPhysAddr(0x100000) != 0x100000) {
        PANIC("Bootstrap identity mapping failed - VALIDATION FAILED");
    }
    const uint64_t probe = IDENTITY_MAP_SIZE - PAGE_SIZE;
    if (VMemGetPhysAddr(probe) != probe) {
        PANIC("Bootstrap identity mapping failed at IDENTITY_MAP_SIZE boundary");
    }
    PrintKernelSuccess("VMem: Buddy allocator initialized with PML4: ");
    PrintKernelHex(pml4_phys_addr);
    PrintKernel("\n");
}

static uint64_t VMemGetPageTablePhys(uint64_t pml4_phys, uint64_t vaddr, uint32_t level, int create) {
    if (!IsValidPhysAddr(pml4_phys)) return 0;

    uint64_t* table_virt = GetTableVirt(pml4_phys);
    uint32_t shift = 39U - (level * 9U);
    uint32_t index = (vaddr >> shift) & PT_INDEX_MASK;

    if (!(table_virt[index] & PAGE_PRESENT)) {
        if (!create) return 0;

        void* new_table_phys = alloc_identity_page_table();
        if (!new_table_phys || !IsValidPhysAddr((uint64_t)new_table_phys)) {
            if (new_table_phys) FreePage(new_table_phys);
            return 0;
        }

        table_virt[index] = (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITABLE;
        return (uint64_t)new_table_phys;
    }

    return table_virt[index] & PT_ADDR_MASK;
}

int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!IsValidPhysAddr(paddr) || !IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 1);
    if (!pt_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    uint64_t* pt_virt = GetTableVirt(pt_phys);
    uint32_t pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    if (pt_virt[pt_index] & PAGE_PRESENT) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    pt_virt[pt_index] = paddr | flags | PAGE_PRESENT;
    add_to_tlb_batch(vaddr);

    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    return VMEM_SUCCESS;
}

int VMemMapHuge(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!IS_HUGE_PAGE_ALIGNED(vaddr) || !IS_HUGE_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!IsValidPhysAddr(paddr) || !IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    uint64_t* pd_virt = GetTableVirt(pd_phys);
    uint32_t pd_index = (vaddr >> PD_SHIFT) & PT_INDEX_MASK;

    if (pd_virt[pd_index] & PAGE_PRESENT) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    pd_virt[pd_index] = paddr | flags | PAGE_PRESENT | PAGE_LARGE;
    add_to_tlb_batch(vaddr);

    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    return VMEM_SUCCESS;
}

void* VMemAlloc(uint64_t size) {
    if (size == 0) return NULL;
    size = PAGE_ALIGN_UP(size);

    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

    uint64_t vaddr = BuddyAlloc(size);
    if (!vaddr) {
        SpinUnlockIrqRestore(&vmem_lock, flags);
        return NULL;
    }

    vmem_allocations++;
    SpinUnlockIrqRestore(&vmem_lock, flags);

    // Map physical pages
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        void* paddr = AllocPage();
        if (!paddr) {
            VMemFree((void*)vaddr, size);
            return NULL;
        }
        if (VMemMap(vaddr + offset, (uint64_t)paddr, PAGE_WRITABLE) != VMEM_SUCCESS) {
            FreePage(paddr);
            VMemFree((void*)vaddr, size);
            return NULL;
        }
    }

    flush_tlb_batch();

    flags = SpinLockIrqSave(&vmem_lock);
    kernel_space.used_pages += size / PAGE_SIZE;
    kernel_space.total_mapped += size;
    SpinUnlockIrqRestore(&vmem_lock, flags);

    FastMemset((void*)vaddr, 0, size);
    return (void*)vaddr;
}

void VMemFree(void* vaddr, uint64_t size) {
    if (!vaddr || size == 0) return;

    uint64_t start_vaddr = PAGE_ALIGN_DOWN((uint64_t)vaddr);
    size = PAGE_ALIGN_UP(size);

    // Unmap all pages and free physical frames
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t current_vaddr = start_vaddr + offset;
        uint64_t paddr = VMemGetPhysAddr(current_vaddr);
        if (paddr != 0) {
            VMemUnmap(current_vaddr, PAGE_SIZE);
            FreePage((void*)paddr);
        }
    }

    flush_tlb_batch();

    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
    BuddyFree(start_vaddr, size);
    kernel_space.used_pages -= size / PAGE_SIZE;
    kernel_space.total_mapped -= size;
    vmem_frees++;
    SpinUnlockIrqRestore(&vmem_lock, flags);
}

void* VMemAllocWithGuards(uint64_t size) {
    if (size == 0) return NULL;
    size = PAGE_ALIGN_UP(size);

    // Allocate space for the user area plus two guard pages
    uint64_t total_size = size + (2 * PAGE_SIZE);
    void* base_ptr = VMemAlloc(total_size);
    if (!base_ptr) return NULL;

    uint64_t base_addr = (uint64_t)base_ptr;
    uint64_t guard1_vaddr = base_addr;
    uint64_t guard2_vaddr = base_addr + size + PAGE_SIZE;

    // Get the physical pages backing the guard pages
    uint64_t paddr1 = VMemGetPhysAddr(guard1_vaddr);
    uint64_t paddr2 = VMemGetPhysAddr(guard2_vaddr);

    // Unmap the guard pages - any access will now cause a page fault
    VMemUnmap(guard1_vaddr, PAGE_SIZE);
    VMemUnmap(guard2_vaddr, PAGE_SIZE);

    // Return the physical pages to the allocator
    if (paddr1) FreePage((void*)paddr1);
    if (paddr2) FreePage((void*)paddr2);

    // Return the address of the usable memory region
    return (void*)(base_addr + PAGE_SIZE);
}

void VMemFreeWithGuards(void* ptr, uint64_t size) {
    if (!ptr) return;
    size = PAGE_ALIGN_UP(size);

    // Calculate the original base address including the first guard page
    uint64_t base_addr = (uint64_t)ptr - PAGE_SIZE;
    uint64_t total_size = size + (2 * PAGE_SIZE);

    // We don't need to check for corruption; a #PF would have already occurred.
    VMemFree((void*)base_addr, total_size);
}

uint64_t VMemGetPhysAddr(uint64_t vaddr) {
    uint64_t pml4_phys = (uint64_t)kernel_space.pml4;
    uint64_t pdp_phys = VMemGetPageTablePhys(pml4_phys, vaddr, 0, 0);
    if (!pdp_phys) return 0;

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 0);
    if (!pd_phys) return 0;

    uint64_t* pd_virt = GetTableVirt(pd_phys);
    uint32_t pd_index = (vaddr >> PD_SHIFT) & PT_INDEX_MASK;
    uint64_t pde = pd_virt[pd_index];

    if (!(pde & PAGE_PRESENT)) return 0;

    if (pde & PAGE_LARGE) {
        uint64_t base = pde & PT_ADDR_MASK;
        return (base & ~(HUGE_PAGE_SIZE - 1)) | (vaddr & (HUGE_PAGE_SIZE - 1));
    }

    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 0);
    if (!pt_phys) return 0;

    uint64_t* pt_virt = GetTableVirt(pt_phys);
    uint32_t pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;
    uint64_t pte = pt_virt[pt_index];

    if (!(pte & PAGE_PRESENT)) return 0;
    return (pte & PT_ADDR_MASK) | (vaddr & PAGE_MASK);
}

int VMemIsPageMapped(uint64_t vaddr) {
    return VMemGetPhysAddr(vaddr) != 0;
}

void VMemFlushTLB(void) {
    __asm__ volatile(
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}

void VMemFlushTLBSingle(uint64_t vaddr) {
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    tlb_flushes++;
}

int VMemUnmap(uint64_t vaddr, uint64_t size) {
    if (size == 0) return VMEM_SUCCESS;

    uint64_t start = PAGE_ALIGN_DOWN(vaddr);
    uint64_t end = PAGE_ALIGN_UP(vaddr + size);
    uint64_t num_pages = (end - start) / PAGE_SIZE;

    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = start + (i * PAGE_SIZE);

        uint64_t pml4_phys = (uint64_t)kernel_space.pml4;
        uint64_t pdp_phys = VMemGetPageTablePhys(pml4_phys, current_vaddr, 0, 0);
        if (!pdp_phys) continue;

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) continue;

        uint64_t* pd_virt = GetTableVirt(pd_phys);
        uint32_t pd_index = (current_vaddr >> PD_SHIFT) & PT_INDEX_MASK;
        uint64_t pde = pd_virt[pd_index];

        if ((pde & PAGE_PRESENT) && (pde & PAGE_LARGE)) {
            if (IS_HUGE_PAGE_ALIGNED(current_vaddr) && (end - current_vaddr) >= HUGE_PAGE_SIZE) {
                pd_virt[pd_index] = 0;
                kernel_space.used_pages -= (HUGE_PAGE_SIZE / PAGE_SIZE);
                kernel_space.total_mapped -= HUGE_PAGE_SIZE;
                add_to_tlb_batch(current_vaddr);
                i += (HUGE_PAGE_SIZE / PAGE_SIZE) - 1;
                continue;
            }
        }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) continue;

        uint64_t* pt_virt = GetTableVirt(pt_phys);
        uint32_t pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            pt_virt[pt_index] = 0;
            kernel_space.used_pages--;
            kernel_space.total_mapped -= PAGE_SIZE;
            add_to_tlb_batch(current_vaddr);
        }
    }

    flush_tlb_batch();
    SpinUnlockIrqRestore(&vmem_lock, flags);
    return VMEM_SUCCESS;
}

/**
 * @brief Gets virtual memory statistics
 */
void VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped) {
    SpinLock(&vmem_lock);
    if (used_pages) *used_pages = kernel_space.used_pages;
    if (total_mapped) *total_mapped = kernel_space.total_mapped;
    SpinUnlock(&vmem_lock);
}

void PrintVMemStats(void) {
    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
    uint64_t used = kernel_space.used_pages;
    uint64_t mapped = kernel_space.total_mapped;
    uint64_t allocs = vmem_allocations;
    uint64_t frees = vmem_frees;
    uint64_t flushes = tlb_flushes;
    SpinUnlockIrqRestore(&vmem_lock, flags);

    PrintKernel("[VMEM] Stats:\n");
    PrintKernel("  Used pages: "); PrintKernelInt(used); PrintKernel("\n");
    PrintKernel("  Mapped: "); PrintKernelInt(mapped / (1024 * 1024)); PrintKernel("MB\n");
    PrintKernel("  Allocs: "); PrintKernelInt(allocs); PrintKernel(", Frees: "); PrintKernelInt(frees); PrintKernel("\n");
    PrintKernel("  TLB flushes: "); PrintKernelInt(flushes); PrintKernel("\n");
}

uint64_t VMemGetPML4PhysAddr(void) {
    return (uint64_t)kernel_space.pml4;  // This is already physical
}

int VMemMapMMIO(uint64_t vaddr, uint64_t paddr, uint64_t size, uint64_t flags) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr) || !IS_PAGE_ALIGNED(size)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    uint64_t mmio_flags = flags | PAGE_PRESENT | PAGE_NOCACHE | PAGE_WRITETHROUGH;
    uint64_t num_pages = size / PAGE_SIZE;

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);
        uint64_t current_paddr = paddr + (i * PAGE_SIZE);

        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 1);
        if (!pdp_phys) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            return VMEM_ERROR_NOMEM;
        }

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 1);
        if (!pd_phys) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            return VMEM_ERROR_NOMEM;
        }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 1);
        if (!pt_phys) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            return VMEM_ERROR_NOMEM;
        }

        uint64_t* pt_virt = GetTableVirt(pt_phys);
        uint32_t pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            return VMEM_ERROR_ALREADY_MAPPED;
        }

        pt_virt[pt_index] = current_paddr | mmio_flags;
        add_to_tlb_batch(current_vaddr);
    }

    flush_tlb_batch();
    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    __asm__ volatile("mfence" ::: "memory");
    return VMEM_SUCCESS;
}

void VMemUnmapMMIO(uint64_t vaddr, uint64_t size) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size) || size == 0) {
        return;
    }

    uint64_t num_pages = size / PAGE_SIZE;
    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);
    uint64_t pml4_phys = VMemGetPML4PhysAddr();

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);

        uint64_t pdp_phys = VMemGetPageTablePhys(pml4_phys, current_vaddr, 0, 0);
        if (!pdp_phys) continue;

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) continue;

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) continue;

        uint64_t* pt_table = GetTableVirt(pt_phys);
        uint32_t pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_table[pt_index] & PAGE_PRESENT) {
            pt_table[pt_index] = 0;
            add_to_tlb_batch(current_vaddr);
        }
    }

    flush_tlb_batch();
    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
}

void* VMemAllocStack(uint64_t size) {
    if (size == 0) return NULL;

    uint64_t stack_size = PAGE_ALIGN_UP(size);
    // We need space for the stack itself, plus one guard page at the bottom.
    uint64_t total_size = stack_size + PAGE_SIZE;

    // Allocate the entire region (guard + stack)
    void* base_ptr = VMemAlloc(total_size);
    if (!base_ptr) return NULL;

    uint64_t base_addr = (uint64_t)base_ptr;

    // The guard page is the very first page in the allocation.
    uint64_t guard_page_vaddr = base_addr;

    // Get the physical page backing the guard page so we can free it.
    uint64_t paddr_guard = VMemGetPhysAddr(guard_page_vaddr);

    // Unmap the guard page. Any write to it will now cause a #PF.
    VMemUnmap(guard_page_vaddr, PAGE_SIZE);

    // Return the physical page to the system's page allocator.
    if (paddr_guard) {
        FreePage((void*)paddr_guard);
    }

    // IMPORTANT: The stack pointer must start at the TOP of the allocated region.
    // The top is the base address + the total size allocated.
    uint64_t stack_top = base_addr + total_size;

    return (void*)stack_top;
}

void VMemFreeStack(void* stack_top, uint64_t size) {
    if (!stack_top || size == 0) return;

    uint64_t stack_size = PAGE_ALIGN_UP(size);
    uint64_t total_size = stack_size + PAGE_SIZE;

    // Re-calculate the original base address of the allocation.
    uint64_t top_addr = (uint64_t)stack_top;
    uint64_t base_addr = top_addr - total_size;

    // The guard page is already unmapped. VMemFree will handle the virtual space.
    // We just need to free the original allocation block.
    VMemFree((void*)base_addr, total_size);
}

void VMemDumpFreeList(void) {
    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
    PrintKernel("[VMEM] Buddy Allocator Free Blocks:\n");

    uint64_t total_free = 0;
    for (uint32_t order = 0; order < BUDDY_NUM_ORDERS; order++) {
        uint32_t count = 0;
        VMemFreeBlock* current = buddy_free_lists[order];
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
    SpinUnlockIrqRestore(&vmem_lock, flags);
}