#include <dynamic/cpp/BuddyAllocator.h>
#include <VMem.h>
#include <Console.h>
#include <MemOps.h>
#include <PMem.h>
#include <Panic.h>
#include <kernel/atomic/cpp/Spinlock.h>

// Global variable for dynamic identity mapping size
uint64_t g_identity_map_size = 4ULL * 1024 * 1024 * 1024; // Default to 4GB, will be updated during init

static VirtAddrSpace kernel_space;
static Spinlock vmem_lock;
static uint64_t vmem_allocations = 0;
static uint64_t vmem_frees = 0;
static uint64_t tlb_flushes = 0;

extern BuddyAllocator g_buddy_allocator;

// TLB flush batching for efficiency
#define MAX_TLB_BATCH 64
static uint64_t tlb_batch[MAX_TLB_BATCH];
static uint32_t tlb_batch_count = 0;

// Identity-mapped page table cache for better allocation
#define PT_CACHE_SIZE 16
static void* pt_cache[PT_CACHE_SIZE];
static uint32_t pt_cache_count = 0;

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
           reinterpret_cast<uint64_t*>(phys_addr) : static_cast<uint64_t*>(PHYS_TO_VIRT(phys_addr));
}

static void flush_tlb_batch() {
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

static void* alloc_identity_page_table() {
    if (pt_cache_count > 0) {
        return pt_cache[--pt_cache_count];
    }

    for (uint32_t attempt = 0; attempt < 32; attempt++) {
        void* candidate = AllocPage();
        if (!candidate) break;
        if (reinterpret_cast<uint64_t>(candidate) < IDENTITY_MAP_SIZE) {
            FastZeroPage(candidate);
            return candidate;
        }
        FreePage(candidate);
    }
    return nullptr;
}

static void cache_page_table(void* pt) {
    if (pt_cache_count < PT_CACHE_SIZE && reinterpret_cast<uint64_t>(pt) < IDENTITY_MAP_SIZE) {
        pt_cache[pt_cache_count++] = pt;
    } else {
        FreePage(pt);
    }
}

void VMemInit() {
    // The VMem system is split into two regions, so we create two buddy allocators.
    uint64_t low_region_size = VIRT_ADDR_SPACE_LOW_END - VIRT_ADDR_SPACE_LOW_START + 1;
    BuddyAllocator_Create(VIRT_ADDR_SPACE_LOW_START, low_region_size);

    // Get current PML4 from CR3 (set by bootstrap)
    uint64_t pml4_phys_addr;
    __asm__ volatile("mov %%cr3, %0" : "=r"(pml4_phys_addr));
    pml4_phys_addr &= ~0xFFF;

    kernel_space.pml4 = reinterpret_cast<uint64_t *>(pml4_phys_addr);
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
        if (!new_table_phys || !IsValidPhysAddr(reinterpret_cast<uint64_t>(new_table_phys))) {
            if (new_table_phys) FreePage(new_table_phys);
            return 0;
        }

        table_virt[index] = reinterpret_cast<uint64_t>(new_table_phys) | PAGE_PRESENT | PAGE_WRITABLE;
        return reinterpret_cast<uint64_t>(new_table_phys);
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

    SpinlockGuard lock(vmem_lock);

    uint64_t pdp_phys = VMemGetPageTablePhys(reinterpret_cast<uint64_t>(kernel_space.pml4), vaddr, 0, 1);
    if (!pdp_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 1);
    if (!pt_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t* pt_virt = GetTableVirt(pt_phys);
    uint32_t pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    if (pt_virt[pt_index] & PAGE_PRESENT) {
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    pt_virt[pt_index] = paddr | flags | PAGE_PRESENT;
    add_to_tlb_batch(vaddr);

    return VMEM_SUCCESS;
}

int VMemMapHuge(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!IS_HUGE_PAGE_ALIGNED(vaddr) || !IS_HUGE_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!IsValidPhysAddr(paddr) || !IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    SpinlockGuard lock(vmem_lock);

    uint64_t pdp_phys = VMemGetPageTablePhys(reinterpret_cast<uint64_t>(kernel_space.pml4), vaddr, 0, 1);
    if (!pdp_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t* pd_virt = GetTableVirt(pd_phys);
    uint32_t pd_index = (vaddr >> PD_SHIFT) & PT_INDEX_MASK;

    if (pd_virt[pd_index] & PAGE_PRESENT) {
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    pd_virt[pd_index] = paddr | flags | PAGE_PRESENT | PAGE_LARGE;
    add_to_tlb_batch(vaddr);

    return VMEM_SUCCESS;
}

void* VMemAlloc(uint64_t size) {
    if (size == 0) return nullptr;
    size = PAGE_ALIGN_UP(size);

    uint64_t vaddr;
    {
        SpinlockGuard lock(vmem_lock);
        vaddr = BuddyAllocator_Allocate(&g_buddy_allocator, size);
        if (!vaddr) {
            return nullptr;
        }
        vmem_allocations++;
    }

    // Map physical pages
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        void* paddr = AllocPage();

        if (VMemMap(vaddr + offset, reinterpret_cast<uint64_t>(paddr), PAGE_WRITABLE) != VMEM_SUCCESS) {
            FreePage(paddr);
            VMemFree(reinterpret_cast<void*>(vaddr), size);
            return nullptr;
        }
    }

    flush_tlb_batch();

    {
        SpinlockGuard lock(vmem_lock);
        kernel_space.used_pages += size / PAGE_SIZE;
        kernel_space.total_mapped += size;
    }

    FastMemset(reinterpret_cast<void*>(vaddr), 0, size);
    return reinterpret_cast<void*>(vaddr);
}

void VMemFree(void* vaddr, uint64_t size) {
    if (!vaddr || size == 0) return;

    uint64_t start_vaddr = PAGE_ALIGN_DOWN(reinterpret_cast<uint64_t>(vaddr));
    size = PAGE_ALIGN_UP(size);

    // Unmap all pages and free physical frames
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t current_vaddr = start_vaddr + offset;
        uint64_t paddr = VMemGetPhysAddr(current_vaddr);
        if (paddr != 0) {
            VMemUnmap(current_vaddr, PAGE_SIZE);
            FreePage(reinterpret_cast<void*>(paddr));
        }
    }

    flush_tlb_batch();

    SpinlockGuard lock(vmem_lock);
    BuddyAllocator_Free(&g_buddy_allocator, start_vaddr, size);
    kernel_space.used_pages -= size / PAGE_SIZE;
    kernel_space.total_mapped -= size;
    vmem_frees++;
}

void* VMemAllocWithGuards(uint64_t size) {
    if (size == 0) return nullptr;
    size = PAGE_ALIGN_UP(size);

    uint64_t total_size = size + (2 * PAGE_SIZE);
    void* base_ptr = VMemAlloc(total_size);
    if (!base_ptr) return nullptr;

    // Allocate space for the user area plus two guard pages
    uint64_t base_addr = reinterpret_cast<uint64_t>(base_ptr);
    uint64_t guard1_vaddr = base_addr;
    uint64_t guard2_vaddr = base_addr + size + PAGE_SIZE;

    // Get the physical pages backing the guard pages
    uint64_t paddr1 = VMemGetPhysAddr(guard1_vaddr);
    uint64_t paddr2 = VMemGetPhysAddr(guard2_vaddr);

    // Unmap the guard pages - any access will now cause a page fault
    VMemUnmap(guard1_vaddr, PAGE_SIZE);
    VMemUnmap(guard2_vaddr, PAGE_SIZE);

    // Return the physical pages to the allocator
    if (paddr1) FreePage(reinterpret_cast<void*>(paddr1));
    if (paddr2) FreePage(reinterpret_cast<void*>(paddr2));

    // Return the address of the usable memory region
    return reinterpret_cast<void*>(base_addr + PAGE_SIZE);
}

void VMemFreeWithGuards(void* ptr, uint64_t size) {
    if (!ptr) return;
    size = PAGE_ALIGN_UP(size);

    // Calculate the original base address including the first guard page
    uint64_t base_addr = reinterpret_cast<uint64_t>(ptr) - PAGE_SIZE;
    uint64_t total_size = size + (2 * PAGE_SIZE);

    // We don't need to check for corruption; a #PF would have already occurred.
    VMemFree(reinterpret_cast<void *>(base_addr), total_size);
}

uint64_t VMemGetPhysAddr(const uint64_t vaddr) {
    const auto pml4_phys = reinterpret_cast<uint64_t>(kernel_space.pml4);
    const uint64_t pdp_phys = VMemGetPageTablePhys(pml4_phys, vaddr, 0, 0);
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

void VMemFlushTLB() {
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

    SpinlockGuard lock(vmem_lock);

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = start + (i * PAGE_SIZE);

        auto pml4_phys = reinterpret_cast<uint64_t>(kernel_space.pml4);
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
    return VMEM_SUCCESS;
}

/**
 * @brief Gets virtual memory statistics
 */
void VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped) {
    SpinlockGuard lock(vmem_lock);
    if (used_pages) *used_pages = kernel_space.used_pages;
    if (total_mapped) *total_mapped = kernel_space.total_mapped;
}

void PrintVMemStats() {
    SpinlockGuard lock(vmem_lock);
    uint64_t used = kernel_space.used_pages;
    uint64_t mapped = kernel_space.total_mapped;
    uint64_t allocs = vmem_allocations;
    uint64_t frees = vmem_frees;
    uint64_t flushes = tlb_flushes;

    PrintKernel("[VMEM] Stats:\n");
    PrintKernel("  Used pages: "); PrintKernelInt(used); PrintKernel("\n");
    PrintKernel("  Mapped: "); PrintKernelInt(mapped / (1024 * 1024)); PrintKernel("MB\n");
    PrintKernel("  Allocs: "); PrintKernelInt(allocs); PrintKernel(", Frees: "); PrintKernelInt(frees); PrintKernel("\n");
    PrintKernel("  TLB flushes: "); PrintKernelInt(flushes); PrintKernel("\n");
}

uint64_t VMemGetPML4PhysAddr() {
    return reinterpret_cast<uint64_t>(kernel_space.pml4);  // This is already physical
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

    SpinlockGuard lock(vmem_lock);

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);
        uint64_t current_paddr = paddr + (i * PAGE_SIZE);

        uint64_t pdp_phys = VMemGetPageTablePhys(reinterpret_cast<uint64_t>(kernel_space.pml4), current_vaddr, 0, 1);
        if (!pdp_phys) {
            return VMEM_ERROR_NOMEM;
        }

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 1);
        if (!pd_phys) {
            return VMEM_ERROR_NOMEM;
        }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 1);
        if (!pt_phys) {
            return VMEM_ERROR_NOMEM;
        }

        uint64_t* pt_virt = GetTableVirt(pt_phys);
        uint32_t pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            return VMEM_ERROR_ALREADY_MAPPED;
        }

        pt_virt[pt_index] = current_paddr | mmio_flags;
        add_to_tlb_batch(current_vaddr);
    }

    flush_tlb_batch();
    __asm__ volatile("mfence" ::: "memory");
    return VMEM_SUCCESS;
}

void VMemUnmapMMIO(uint64_t vaddr, uint64_t size) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size) || size == 0) {
        return;
    }

    uint64_t num_pages = size / PAGE_SIZE;
    SpinlockGuard lock(vmem_lock);
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
}

void* VMemAllocStack(uint64_t size) {
    if (size == 0) return nullptr;

    uint64_t stack_size = PAGE_ALIGN_UP(size);
    // We need space for the stack itself, plus one guard page at the bottom.
    uint64_t total_size = stack_size + PAGE_SIZE;

    // Allocate the entire region (guard + stack)
    void* base_ptr = VMemAlloc(total_size);
    if (!base_ptr) return nullptr;

    const auto base_addr = reinterpret_cast<uint64_t>(base_ptr);

    // The guard page is the very first page in the allocation.
    uint64_t guard_page_vaddr = base_addr;

    // Get the physical page backing the guard page so we can free it.
    uint64_t paddr_guard = VMemGetPhysAddr(guard_page_vaddr);

    // Unmap the guard page. Any write to it will now cause a #PF.
    VMemUnmap(guard_page_vaddr, PAGE_SIZE);

    // Return the physical page to the system's page allocator.
    if (paddr_guard) {
        FreePage(reinterpret_cast<void *>(paddr_guard));
    }

    // IMPORTANT: The stack pointer must start at the TOP of the allocated region.
    // The top is the base address + the total size allocated.
    uint64_t stack_top = base_addr + total_size;

    return reinterpret_cast<void *>(stack_top);
}

void VMemFreeStack(void* stack_top, uint64_t size) {
    if (!stack_top || size == 0) return;

    const uint64_t stack_size = PAGE_ALIGN_UP(size);
    const uint64_t total_size = stack_size + PAGE_SIZE;

    // Re-calculate the original base address of the allocation.
    const auto top_addr = reinterpret_cast<uint64_t>(stack_top);
    const uint64_t base_addr = top_addr - total_size;

    // The guard page is already unmapped. VMemFree will handle the virtual space.
    // We just need to free the original allocation block.
    VMemFree(reinterpret_cast<void *>(base_addr), total_size);
}

void VMemDumpFreeList() {
    BuddyAllocator_DumpFreeList(&g_buddy_allocator);
}