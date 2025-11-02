#include <VMem.h>
#include <Console.h>
#include <MemOps.h>
#include <PMem.h>
#include <Panic.h>
#include <kernel/atomic/cpp/Spinlock.h>
#include <dynamic/cpp/BuddyAllocator.h>

// Global variable for dynamic identity mapping size
uint64_t g_identity_map_size = 4ULL * 1024 * 1024 * 1024; // Default to 4GB, will be updated during init

extern BuddyAllocator g_buddy_allocator;
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

#define MAX_TLB_BATCH 64
#define PT_CACHE_SIZE 16

static VirtAddrSpace kernel_space;
static Spinlock vmem_lock;
static uint64_t vmem_allocations;
static uint64_t vmem_frees;
static uint64_t tlb_flushes;

static uint64_t tlb_batch[MAX_TLB_BATCH];
static uint32_t tlb_batch_count;

static void* pt_cache[PT_CACHE_SIZE];
static uint32_t pt_cache_count;


void VMM::init() {
    vmem_allocations = 0;
    vmem_frees = 0;
    tlb_flushes = 0;
    tlb_batch_count = 0;
    pt_cache_count = 0;
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
    if (VMM_VMemGetPhysAddr(0x100000) != 0x100000) {
        PANIC("Bootstrap identity mapping failed - VALIDATION FAILED");
    }
    if (const uint64_t probe = IDENTITY_MAP_SIZE - PAGE_SIZE; VMM_VMemGetPhysAddr(probe) != probe) {
        PANIC("Bootstrap identity mapping failed at IDENTITY_MAP_SIZE boundary");
    }
    PrintKernelSuccess("VMem: Buddy allocator initialized with PML4: ");
    PrintKernelHex(pml4_phys_addr);
    PrintKernel("\n");
}

int VMM::VMM_VMemMap(const uint64_t vaddr, const uint64_t paddr, const uint64_t flags) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!VMM_IsValidPhysAddr(paddr) || !VMM_IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    SpinlockGuard lock(vmem_lock);

    const uint64_t pdp_phys = VMM_VMemGetPageTablePhys(reinterpret_cast<uint64_t>(kernel_space.pml4), vaddr, 0, 1);
    if (!pdp_phys) {
        return VMEM_ERROR_NOMEM;
    }

    const uint64_t pd_phys = VMM_VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        return VMEM_ERROR_NOMEM;
    }

    const uint64_t pt_phys = VMM_VMemGetPageTablePhys(pd_phys, vaddr, 2, 1);
    if (!pt_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t* pt_virt = VMM_GetTableVirt(pt_phys);
    const uint32_t pt_index = vaddr >> PT_SHIFT & PT_INDEX_MASK;

    if (pt_virt[pt_index] & PAGE_PRESENT) {
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    pt_virt[pt_index] = paddr | flags | PAGE_PRESENT;
    VMM_add_to_tlb_batch(vaddr);

    return VMEM_SUCCESS;
}

int VMM::VMM_VMemMapHuge(const uint64_t vaddr, const uint64_t paddr, const uint64_t flags) {
    if (!IS_HUGE_PAGE_ALIGNED(vaddr) || !IS_HUGE_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!VMM_IsValidPhysAddr(paddr) || !VMM_IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    SpinlockGuard lock(vmem_lock);

    const uint64_t pdp_phys = VMM_VMemGetPageTablePhys(reinterpret_cast<uint64_t>(kernel_space.pml4), vaddr, 0, 1);
    if (!pdp_phys) {
        return VMEM_ERROR_NOMEM;
    }

    const uint64_t pd_phys = VMM_VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        return VMEM_ERROR_NOMEM;
    }

    uint64_t* pd_virt = VMM_GetTableVirt(pd_phys);
    const uint32_t pd_index = vaddr >> PD_SHIFT & PT_INDEX_MASK;

    if (pd_virt[pd_index] & PAGE_PRESENT) {
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    pd_virt[pd_index] = paddr | flags | PAGE_PRESENT | PAGE_LARGE;
    VMM_add_to_tlb_batch(vaddr);

    return VMEM_SUCCESS;
}

void* VMM::VMM_VMemAlloc(const uint64_t size_p) {
    if (size_p == 0) return nullptr;
    const uint64_t size = PAGE_ALIGN_UP(size_p);

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
        if (void* paddr = AllocPage(); VMM_VMemMap(vaddr + offset, reinterpret_cast<uint64_t>(paddr), PAGE_WRITABLE) != VMEM_SUCCESS) {
            FreePage(paddr);
            VMM_VMemFree(reinterpret_cast<void*>(vaddr), size);
            return nullptr;
        }
    }

    VMM_flush_tlb_batch();

    {
        SpinlockGuard lock(vmem_lock);
        kernel_space.used_pages += size / PAGE_SIZE;
        kernel_space.total_mapped += size;
    }

    FastMemset(reinterpret_cast<void*>(vaddr), 0, size);
    return reinterpret_cast<void*>(vaddr);
}

void VMM::VMM_VMemFree(void* vaddr, const uint64_t size_p) {
    if (!vaddr || size_p == 0) return;

    const uint64_t start_vaddr = PAGE_ALIGN_DOWN(reinterpret_cast<uint64_t>(vaddr));
    const uint64_t size = PAGE_ALIGN_UP(size_p);

    // Unmap all pages and free physical frames
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        const uint64_t current_vaddr = start_vaddr + offset;
        if (const uint64_t paddr = VMM_VMemGetPhysAddr(current_vaddr); paddr != 0) {
            VMM_VMemUnmap(current_vaddr, PAGE_SIZE);
            FreePage(reinterpret_cast<void*>(paddr));
        }
    }

    VMM_flush_tlb_batch();

    SpinlockGuard lock(vmem_lock);
    BuddyAllocator_Free(&g_buddy_allocator, start_vaddr, size);
    kernel_space.used_pages -= size / PAGE_SIZE;
    kernel_space.total_mapped -= size;
    vmem_frees++;
}

void* VMM::VMM_VMemAllocWithGuards(const uint64_t size_p) {
    if (size_p == 0) return nullptr;
    const uint64_t size = PAGE_ALIGN_UP(size_p);

    const uint64_t total_size = size + 2 * PAGE_SIZE;
    void* base_ptr = VMemAlloc(total_size);
    if (!base_ptr) return nullptr;

    // Allocate space for the user area plus two guard pages
    const auto base_addr = reinterpret_cast<uint64_t>(base_ptr);
    const uint64_t guard1_vaddr = base_addr;
    const uint64_t guard2_vaddr = base_addr + size + PAGE_SIZE;

    // Get the physical pages backing the guard pages
    const uint64_t paddr1 = VMM_VMemGetPhysAddr(guard1_vaddr);
    const uint64_t paddr2 = VMM_VMemGetPhysAddr(guard2_vaddr);

    // Unmap the guard pages - any access will now cause a page fault
    VMM_VMemUnmap(guard1_vaddr, PAGE_SIZE);
    VMM_VMemUnmap(guard2_vaddr, PAGE_SIZE);

    // Return the physical pages to the allocator
    if (paddr1) FreePage(reinterpret_cast<void*>(paddr1));
    if (paddr2) FreePage(reinterpret_cast<void*>(paddr2));

    // Return the address of the usable memory region
    return reinterpret_cast<void*>(base_addr + PAGE_SIZE);
}

void VMM::VMM_VMemFreeWithGuards(void* ptr, const uint64_t size_p) {
    if (!ptr) return;
    const uint64_t size = PAGE_ALIGN_UP(size_p);

    // Calculate the original base address including the first guard page
    const uint64_t base_addr = reinterpret_cast<uint64_t>(ptr) - PAGE_SIZE;
    const uint64_t total_size = size + 2 * PAGE_SIZE;

    // We don't need to check for corruption; a #PF would have already occurred.
    VMM_VMemFree(reinterpret_cast<void *>(base_addr), total_size);
}

uint64_t VMM::VMM_VMemGetPhysAddr(const uint64_t vaddr) {
    const auto pml4_phys = reinterpret_cast<uint64_t>(kernel_space.pml4);
    const uint64_t pdp_phys = VMM_VMemGetPageTablePhys(pml4_phys, vaddr, 0, 0);
    if (!pdp_phys) return 0;

    const uint64_t pd_phys = VMM_VMemGetPageTablePhys(pdp_phys, vaddr, 1, 0);
    if (!pd_phys) return 0;

    const uint64_t* pd_virt = VMM_GetTableVirt(pd_phys);
    const uint32_t pd_index = vaddr >> PD_SHIFT & PT_INDEX_MASK;
    const uint64_t pde = pd_virt[pd_index];

    if (!(pde & PAGE_PRESENT)) return 0;

    if (pde & PAGE_LARGE) {
        const uint64_t base = pde & PT_ADDR_MASK;
        return base & ~(HUGE_PAGE_SIZE - 1) | vaddr & HUGE_PAGE_SIZE - 1;
    }

    const uint64_t pt_phys = VMM_VMemGetPageTablePhys(pd_phys, vaddr, 2, 0);
    if (!pt_phys) return 0;

    const uint64_t* pt_virt = VMM_GetTableVirt(pt_phys);
    const uint32_t pt_index = vaddr >> PT_SHIFT & PT_INDEX_MASK;
    const uint64_t pte = pt_virt[pt_index];

    if (!(pte & PAGE_PRESENT)) return 0;
    return pte & PT_ADDR_MASK | vaddr & PAGE_MASK;
}

int VMM::VMM_VMemUnmap(const uint64_t vaddr, const uint64_t size) {
    if (size == 0) return VMEM_SUCCESS;

    const uint64_t start = PAGE_ALIGN_DOWN(vaddr);
    const uint64_t end = PAGE_ALIGN_UP(vaddr + size);
    const uint64_t num_pages = (end - start) / PAGE_SIZE;

    SpinlockGuard lock(vmem_lock);

    for (uint64_t i = 0; i < num_pages; i++) {
        const uint64_t current_vaddr = start + i * PAGE_SIZE;

        const auto pml4_phys = reinterpret_cast<uint64_t>(kernel_space.pml4);
        const uint64_t pdp_phys = VMM_VMemGetPageTablePhys(pml4_phys, current_vaddr, 0, 0);
        if (!pdp_phys) continue;

        const uint64_t pd_phys = VMM_VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) continue;

        uint64_t* pd_virt = VMM_GetTableVirt(pd_phys);
        const uint32_t pd_index = current_vaddr >> PD_SHIFT & PT_INDEX_MASK;

        if (const uint64_t pde = pd_virt[pd_index]; pde & PAGE_PRESENT && pde & PAGE_LARGE) {
            if (IS_HUGE_PAGE_ALIGNED(current_vaddr) && end - current_vaddr >= HUGE_PAGE_SIZE) {
                pd_virt[pd_index] = 0;
                kernel_space.used_pages -= HUGE_PAGE_SIZE / PAGE_SIZE;
                kernel_space.total_mapped -= HUGE_PAGE_SIZE;
                VMM_add_to_tlb_batch(current_vaddr);
                i += HUGE_PAGE_SIZE / PAGE_SIZE - 1;
                continue;
            }
        }

        const uint64_t pt_phys = VMM_VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) continue;

        uint64_t* pt_virt = VMM_GetTableVirt(pt_phys);

        if (const uint32_t pt_index = current_vaddr >> PT_SHIFT & PT_INDEX_MASK; pt_virt[pt_index] & PAGE_PRESENT) {
            pt_virt[pt_index] = 0;
            kernel_space.used_pages--;
            kernel_space.total_mapped -= PAGE_SIZE;
            VMM_add_to_tlb_batch(current_vaddr);
        }
    }

    VMM_flush_tlb_batch();
    return VMEM_SUCCESS;
}

void VMM::VMM_VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped) {
    SpinlockGuard lock(vmem_lock);
    if (used_pages) *used_pages = kernel_space.used_pages;
    if (total_mapped) *total_mapped = kernel_space.total_mapped;
}

int VMM::VMM_VMemMapMMIO(uint64_t vaddr, uint64_t paddr, uint64_t size, uint64_t flags) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr) || !IS_PAGE_ALIGNED(size)) {
        return VMEM_ERROR_ALIGN;
    }
    if (!VMM_IsValidVirtAddr(vaddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    uint64_t mmio_flags = flags | PAGE_PRESENT | PAGE_NOCACHE | PAGE_WRITETHROUGH;
    uint64_t num_pages = size / PAGE_SIZE;

    SpinlockGuard lock(vmem_lock);

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + i * PAGE_SIZE;
        uint64_t current_paddr = paddr + i * PAGE_SIZE;

        uint64_t pdp_phys = VMM_VMemGetPageTablePhys(reinterpret_cast<uint64_t>(kernel_space.pml4), current_vaddr, 0, 1);
        if (!pdp_phys) {
            return VMEM_ERROR_NOMEM;
        }

        uint64_t pd_phys = VMM_VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 1);
        if (!pd_phys) {
            return VMEM_ERROR_NOMEM;
        }

        uint64_t pt_phys = VMM_VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 1);
        if (!pt_phys) {
            return VMEM_ERROR_NOMEM;
        }

        uint64_t* pt_virt = VMM_GetTableVirt(pt_phys);
        uint32_t pt_index = current_vaddr >> PT_SHIFT & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            return VMEM_ERROR_ALREADY_MAPPED;
        }

        pt_virt[pt_index] = current_paddr | mmio_flags;
        VMM_add_to_tlb_batch(current_vaddr);
    }

    VMM_flush_tlb_batch();
    __asm__ volatile("mfence" ::: "memory");
    return VMEM_SUCCESS;
}

void VMM::VMM_VMemUnmapMMIO(const uint64_t vaddr, const uint64_t size) {
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size) || size == 0) {
        return;
    }

    const uint64_t num_pages = size / PAGE_SIZE;
    SpinlockGuard lock(vmem_lock);
    const uint64_t pml4_phys = VMemGetPML4PhysAddr();

    for (uint64_t i = 0; i < num_pages; i++) {
        const uint64_t current_vaddr = vaddr + i * PAGE_SIZE;

        const uint64_t pdp_phys = VMM_VMemGetPageTablePhys(pml4_phys, current_vaddr, 0, 0);
        if (!pdp_phys) continue;

        const uint64_t pd_phys = VMM_VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) continue;

        const uint64_t pt_phys = VMM_VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) continue;

        uint64_t* pt_table = VMM_GetTableVirt(pt_phys);

        if (const uint32_t pt_index = current_vaddr >> PT_SHIFT & PT_INDEX_MASK;
            pt_table[pt_index] & PAGE_PRESENT) {
            pt_table[pt_index] = 0;
            VMM_add_to_tlb_batch(current_vaddr);
        }
    }

    VMM_flush_tlb_batch();
}

void* VMM::VMM_VMemAllocStack(const uint64_t size) {
    if (size == 0) return nullptr;

    const uint64_t stack_size = PAGE_ALIGN_UP(size);
    // We need space for the stack itself, plus one guard page at the bottom.
    const uint64_t total_size = stack_size + PAGE_SIZE;

    // Allocate the entire region (guard + stack)
    void* base_ptr = VMM_VMemAlloc(total_size);
    if (!base_ptr) return nullptr;

    const auto base_addr = reinterpret_cast<uint64_t>(base_ptr);

    // The guard page is the very first page in the allocation.
    const uint64_t guard_page_vaddr = base_addr;

    // Get the physical page backing the guard page so we can free it.
    const uint64_t paddr_guard = VMM_VMemGetPhysAddr(guard_page_vaddr);

    // Unmap the guard page. Any write to it will now cause a #PF.
    VMM_VMemUnmap(guard_page_vaddr, PAGE_SIZE);

    // Return the physical page to the system's page allocator.
    if (paddr_guard) {
        FreePage(reinterpret_cast<void *>(paddr_guard));
    }

    // IMPORTANT: The stack pointer must start at the TOP of the allocated region.
    // The top is the base address + the total size allocated.
    const uint64_t stack_top = base_addr + total_size;

    return reinterpret_cast<void *>(stack_top);
}

void VMM::VMM_VMemFreeStack(void* stack_top, const uint64_t size) {
    if (!stack_top || size == 0) return;

    const uint64_t stack_size = PAGE_ALIGN_UP(size);
    const uint64_t total_size = stack_size + PAGE_SIZE;

    // Re-calculate the original base address of the allocation.
    const auto top_addr = reinterpret_cast<uint64_t>(stack_top);
    const uint64_t base_addr = top_addr - total_size;

    // The guard page is already unmapped. VMemFree will handle the virtual space.
    // We just need to free the original allocation block.
    VMM_VMemFree(reinterpret_cast<void *>(base_addr), total_size);
}

void VMM::VMM_PrintVMemStats() {
    SpinlockGuard lock(vmem_lock);
    const uint64_t used = kernel_space.used_pages;
    const uint64_t mapped = kernel_space.total_mapped;
    const uint64_t allocs = vmem_allocations;
    const uint64_t frees = vmem_frees;
    const uint64_t flushes = tlb_flushes;

    PrintKernel("[VMEM] Stats:\n");
    PrintKernel("  Used pages: "); PrintKernelInt(static_cast<signed>(used)); PrintKernel("\n");
    PrintKernel("  Mapped: "); PrintKernelInt(static_cast<signed>(mapped / (1024 * 1024))); PrintKernel("MB\n");
    PrintKernel("  Allocs: "); PrintKernelInt(static_cast<signed>(allocs)); PrintKernel(", Frees: ");
    PrintKernelInt(static_cast<signed>(frees)); PrintKernel("\n");
    PrintKernel("  TLB flushes: "); PrintKernelInt(static_cast<signed>(flushes)); PrintKernel("\n");
}

uint64_t VMM::VMM_VMemGetPML4PhysAddr() {
    return reinterpret_cast<uint64_t>(kernel_space.pml4);  // This is already physical
}

int VMM::VMM_IsValidPhysAddr(const uint64_t paddr) {
    return paddr != 0 && paddr < total_pages * PAGE_SIZE;
}

int VMM::VMM_IsValidVirtAddr(const uint64_t vaddr) {
    // Check canonical address ranges
    return (vaddr >= VIRT_ADDR_SPACE_LOW_START && vaddr <= VIRT_ADDR_SPACE_LOW_END) ||
           (vaddr >= VIRT_ADDR_SPACE_HIGH_START && vaddr <= VIRT_ADDR_SPACE_HIGH_END) ||
           (vaddr >= KERNEL_SPACE_START && vaddr <= KERNEL_SPACE_END);
}

uint64_t* VMM::VMM_GetTableVirt(const uint64_t phys_addr) {
    return phys_addr < IDENTITY_MAP_SIZE ?
           reinterpret_cast<uint64_t*>(phys_addr) : static_cast<uint64_t*>(PHYS_TO_VIRT(phys_addr));
}

void VMM::VMM_flush_tlb_batch() {
    if (tlb_batch_count == 0) return;

    if (tlb_batch_count > 8) {
        VMM_VMemFlushTLB();
    } else {
        for (uint32_t i = 0; i < tlb_batch_count; i++) {
            __asm__ volatile("invlpg (%0)" :: "r"(tlb_batch[i]) : "memory");
        }
    }
    tlb_batch_count = 0;
    tlb_flushes++;
}

void VMM::VMM_add_to_tlb_batch(const uint64_t vaddr) {
    if (tlb_batch_count >= MAX_TLB_BATCH) {
        VMM_flush_tlb_batch();
    }
    tlb_batch[tlb_batch_count++] = vaddr;
}

static void* VMM_alloc_identity_page_table() {
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

void VMM::VMM_cache_page_table(void* pt) {
    if (pt_cache_count < PT_CACHE_SIZE && reinterpret_cast<uint64_t>(pt) < IDENTITY_MAP_SIZE) {
        pt_cache[pt_cache_count++] = pt;
    } else {
        FreePage(pt);
    }
}

uint64_t VMM::VMM_VMemGetPageTablePhys(const uint64_t pml4_phys, const uint64_t vaddr, const uint32_t level, const int create) {
    if (!VMM_IsValidPhysAddr(pml4_phys)) return 0;

    uint64_t* table_virt = VMM_GetTableVirt(pml4_phys);
    const uint32_t shift = 39U - level * 9U;
    const uint32_t index = vaddr >> shift & PT_INDEX_MASK;

    if (!(table_virt[index] & PAGE_PRESENT)) {
        if (!create) return 0;

        void* new_table_phys = VMM_alloc_identity_page_table();
        if (!new_table_phys || !VMM_IsValidPhysAddr(reinterpret_cast<uint64_t>(new_table_phys))) {
            if (new_table_phys) FreePage(new_table_phys);
            return 0;
        }

        table_virt[index] = reinterpret_cast<uint64_t>(new_table_phys) | PAGE_PRESENT | PAGE_WRITABLE;
        return reinterpret_cast<uint64_t>(new_table_phys);
    }

    return table_virt[index] & PT_ADDR_MASK;
}

void VMM::VMM_VMemFlushTLB() {
    __asm__ volatile(
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}

void VMM::VMM_VMemFlushTLBSingle(uint64_t vaddr) {
    __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    tlb_flushes++;
}

/**
 *@section C-compatible interface
 */

void VMemInit() {
    VMM::init();
}

int VMemMap(const uint64_t vaddr, const uint64_t paddr, const uint64_t flags) {
    return VMM::VMM_VMemMap(vaddr, paddr, flags);
}

int VMemMapHuge(const uint64_t vaddr, const uint64_t paddr, const uint64_t flags) {
    return VMM::VMM_VMemMapHuge(vaddr, paddr, flags);
}

void* VMemAlloc(const uint64_t size) {
    return VMM::VMM_VMemAlloc(size);
}

void VMemFree(void* vaddr, const uint64_t size) {
    VMM::VMM_VMemFree(vaddr, size);
}

void* VMemAllocWithGuards(const uint64_t size) {
    return VMM::VMM_VMemAllocWithGuards(size);
}

void VMemFreeWithGuards(void* ptr, const uint64_t size) {
    VMM::VMM_VMemFreeWithGuards(ptr, size);
}

uint64_t VMemGetPhysAddr(const uint64_t vaddr) {
    return VMM::VMM_VMemGetPhysAddr(vaddr);
}

int VMemIsPageMapped(const uint64_t vaddr) {
    return VMM::VMM_VMemGetPhysAddr(vaddr) != 0;
}

int VMemUnmap(const uint64_t vaddr, const uint64_t size) {
    return VMM::VMM_VMemUnmap(vaddr, size);
}

void VMemGetStats(uint64_t* used_pages, uint64_t* total_mapped) {
    VMM::VMM_VMemGetStats(used_pages, total_mapped);
}

void PrintVMemStats() {
    VMM::VMM_PrintVMemStats();
}

uint64_t VMemGetPML4PhysAddr() {
    return VMM::VMM_VMemGetPML4PhysAddr();
}

int VMemMapMMIO(const uint64_t vaddr, const uint64_t paddr, const uint64_t size, const uint64_t flags) {
    return VMM::VMM_VMemMapMMIO(vaddr, paddr, size, flags);
}

void VMemUnmapMMIO(const uint64_t vaddr, const uint64_t size) {
    VMM::VMM_VMemUnmapMMIO(vaddr, size);
}

void* VMemAllocStack(const uint64_t size) {
    return VMM::VMM_VMemAllocStack(size);
}

void VMemFreeStack(void* stack_top, const uint64_t size) {
    VMM::VMM_VMemFreeStack(stack_top, size);
}

void VMemDumpFreeList() {
    BuddyAllocator_DumpFreeList(&g_buddy_allocator);
}
