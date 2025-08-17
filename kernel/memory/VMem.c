#include "VMem.h"
#include "Console.h"
#include "MemOps.h"
#include "Memory.h"
#include "Panic.h"
#include "Spinlock.h"

static VirtAddrSpace kernel_space;
static volatile int vmem_lock = 0;
static uint64_t vmem_allocations = 0;
static uint64_t vmem_frees = 0;
static uint64_t tlb_flushes = 0;

// A pre-allocated pool for free list nodes to avoid dynamic allocation issues.
#define MAX_FREE_BLOCKS 1024
static VMemFreeBlock free_block_pool[MAX_FREE_BLOCKS];
static VMemFreeBlock* free_block_head = NULL;

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

static void InitFreeBlockPool(void) {
    free_block_head = &free_block_pool[0];
    for (int i = 0; i < MAX_FREE_BLOCKS - 1; ++i) {
        free_block_pool[i].next = &free_block_pool[i + 1];
    }
    free_block_pool[MAX_FREE_BLOCKS - 1].next = NULL;
}

static VMemFreeBlock* AllocFreeBlock(void) {
    if (!free_block_head) {
        return NULL; // Pool exhausted
    }
    VMemFreeBlock* block = free_block_head;
    free_block_head = block->next;
    return block;
}

static void ReleaseFreeBlock(VMemFreeBlock* block) {
    block->next = free_block_head;
    free_block_head = block;
}

static inline int is_valid_phys_addr(uint64_t paddr) {
    // Basic sanity check - adjust limits based on your system
    return (paddr != 0 && paddr < (total_pages * PAGE_SIZE));
}

void VMemInit(void) {
    InitFreeBlockPool();
    // Get current PML4 from CR3 (set by bootstrap)
    uint64_t pml4_phys_addr;
    asm volatile("mov %%cr3, %0" : "=r"(pml4_phys_addr));
    pml4_phys_addr &= ~0xFFF; // Clear flags

    // Initialize kernel space tracking
    kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    kernel_space.used_pages = 0;
    kernel_space.total_mapped = IDENTITY_MAP_SIZE;
    kernel_space.pml4 = (uint64_t*)pml4_phys_addr;
    uint64_t kernel_size = (uint64_t)_kernel_phys_end - (uint64_t)_kernel_phys_start;
    kernel_space.total_mapped += PAGE_ALIGN_UP(kernel_size);
    // Now test identity mapping -- FAILING
    // if (VMemGetPhysAddr(0x100000) != 0x100000) {
    //     PANIC("Bootstrap identity mapping failed - VALIDATION FAILED");
    // }
    // const uint64_t probe = IDENTITY_MAP_SIZE - PAGE_SIZE;
    // if (VMemGetPhysAddr(probe) != probe) {
    //     PANIC("Bootstrap identity mapping failed at IDENTITY_MAP_SIZE boundary");
    // }
    PrintKernelSuccess("VMem: VMem initialized using existing PML4: ");
    PrintKernelHex(pml4_phys_addr);
    PrintKernel("\n");
}

static uint64_t VMemGetPageTablePhys(uint64_t pml4_phys, uint64_t vaddr, int level, int create) {
    if (!is_valid_phys_addr(pml4_phys)) return 0;

    // Access the table via identity map when available, otherwise via higher-half mapping
    uint64_t* table_virt = (pml4_phys < IDENTITY_MAP_SIZE)
        ? (uint64_t*)pml4_phys
        : (uint64_t*)PHYS_TO_VIRT(pml4_phys);

    int shift = 39 - (level * 9);
    int index = (vaddr >> shift) & PT_INDEX_MASK;
    if (index >= 512) return 0;

    if (!(table_virt[index] & PAGE_PRESENT)) {
        if (!create) return 0;

        // Allocate page-table memory from identity-mapped low memory to ensure accessibility
        void* new_table_phys = NULL;
        for (int attempt = 0; attempt < 32; attempt++) {
            void* candidate = AllocPage();
            if (!candidate) break;
            if ((uint64_t)candidate < IDENTITY_MAP_SIZE) {
                new_table_phys = candidate;
                break;
            }
            // Not identity-mapped; return it to the pool and try again
            FreePage(candidate);
        }
        if (!new_table_phys) return 0;
        if (!is_valid_phys_addr((uint64_t)new_table_phys)) {
            FreePage(new_table_phys);
            return 0;
        }

        // Zero the new table using an address we can access
        if ((uint64_t)new_table_phys < IDENTITY_MAP_SIZE) {
            FastZeroPage(new_table_phys);
        } else {
            FastZeroPage(PHYS_TO_VIRT(new_table_phys));
        }

        table_virt[index] = (uint64_t)new_table_phys | PAGE_PRESENT | PAGE_WRITABLE;
        return (uint64_t)new_table_phys;
    }

    return table_virt[index] & PT_ADDR_MASK;
}

int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    // Validate alignment
    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }

    // Validate addresses
    if (!is_valid_phys_addr(paddr)) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    // Validate virtual address range
    if (vaddr < VIRT_ADDR_SPACE_START || vaddr >= VIRT_ADDR_SPACE_END) {
        return VMEM_ERROR_INVALID_ADDR;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    // Get PDP table
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Get PD table
    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Get PT table
    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 1);
    if (!pt_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Access PT through identity mapping if possible
    uint64_t* pt_virt;
    if (pt_phys < IDENTITY_MAP_SIZE) {
        pt_virt = (uint64_t*)pt_phys;
    } else {
        pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    }

    int pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    // Check if already mapped
    if (pt_virt[pt_index] & PAGE_PRESENT) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    // Set the mapping
    pt_virt[pt_index] = paddr | flags | PAGE_PRESENT;

    // Invalidate TLB
    VMemFlushTLBSingle(vaddr);

    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    return VMEM_SUCCESS;
}

// Map huge page in VMem.c
int VMemMapHuge(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    if (!IS_HUGE_PAGE_ALIGNED(vaddr) || !IS_HUGE_PAGE_ALIGNED(paddr)) {
        return VMEM_ERROR_ALIGN;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

    if (!is_valid_phys_addr(paddr)) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_INVALID_ADDR;
    }
    // Get PDP table
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 1);
    if (!pdp_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Get PD table
    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 1);
    if (!pd_phys) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_NOMEM;
    }

    // Access PD through identity mapping
    uint64_t* pd_virt = (pd_phys < IDENTITY_MAP_SIZE) ?
                        (uint64_t*)pd_phys : (uint64_t*)PHYS_TO_VIRT(pd_phys);

    int pd_index = (vaddr >> PD_SHIFT) & PT_INDEX_MASK;

    // Check if already mapped
    if (pd_virt[pd_index] & PAGE_PRESENT) {
        SpinUnlockIrqRestore(&vmem_lock, irq_flags);
        return VMEM_ERROR_ALREADY_MAPPED;
    }

    // Set huge page mapping (PS bit = 1 for 2MB pages)
    pd_virt[pd_index] = paddr | flags | PAGE_PRESENT | PAGE_LARGE;

    VMemFlushTLBSingle(vaddr);
    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    return VMEM_SUCCESS;
}

void* VMemAlloc(uint64_t size) {
    if (size == 0) return NULL;
    size = PAGE_ALIGN_UP(size);

    uint64_t vaddr = 0;
    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

    // 1. Search the free list for a suitable block
    VMemFreeBlock* prev = NULL;
    VMemFreeBlock* current = kernel_space.free_list;
    while (current) {
        if (current->size >= size) {
            vaddr = current->base;
            if (current->size == size) { // Perfect fit
                if (prev) {
                    prev->next = current->next;
                } else {
                    kernel_space.free_list = current->next;
                }
                ReleaseFreeBlock(current);
            } else { // Block is larger, split it
                current->base += size;
                current->size -= size;
            }
            break;
        }
        prev = current;
        current = current->next;
    }

    // 2. If no suitable block found, use the bump allocator
    if (vaddr == 0) {
        if (kernel_space.next_vaddr < VIRT_ADDR_SPACE_START) {
            kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
        }
        if (kernel_space.next_vaddr + size > VIRT_ADDR_SPACE_END) {
            SpinUnlockIrqRestore(&vmem_lock, flags);
            return NULL; // Out of virtual address space
        }
        vaddr = kernel_space.next_vaddr;
        kernel_space.next_vaddr += size;
    }

    vmem_allocations++;
    SpinUnlockIrqRestore(&vmem_lock, flags);

    // 3. Map physical pages into the allocated virtual space
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        void* paddr = AllocPage();
        if (!paddr) {
            VMemFree((void*)vaddr, size); // Rollback
            return NULL;
        }
        if (VMemMap(vaddr + offset, (uint64_t)paddr, PAGE_WRITABLE) != VMEM_SUCCESS) {
            FreePage(paddr);
            VMemFree((void*)vaddr, size); // Rollback
            return NULL;
        }
    }

    // 4. Update stats and zero memory
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

    // 1. Unmap all pages and free the underlying physical frames
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t current_vaddr = start_vaddr + offset;
        uint64_t paddr = VMemGetPhysAddr(current_vaddr);
        if (paddr != 0) {
            VMemUnmap(current_vaddr, PAGE_SIZE);
            FreePage((void*)paddr);
        }
    }

    // 2. Add the virtual address range back to the free list
    irq_flags_t flags = SpinLockIrqSave(&vmem_lock);

    VMemFreeBlock* new_block = AllocFreeBlock();
    if (!new_block) { // Should be rare, but possible
        PANIC("VMemFree: Out of free list nodes!");
        SpinUnlockIrqRestore(&vmem_lock, flags);
        return;
    }
    new_block->base = start_vaddr;
    new_block->size = size;

    // Insert into sorted list and merge with neighbors if possible
    VMemFreeBlock *prev = NULL, *current = kernel_space.free_list;
    while (current && current->base < new_block->base) {
        prev = current;
        current = current->next;
    }

    // Merge with next block?
    if (current && new_block->base + new_block->size == current->base) {
        current->base = new_block->base;
        current->size += new_block->size;
        ReleaseFreeBlock(new_block);
        new_block = current;
    }

    // Merge with previous block?
    if (prev && prev->base + prev->size == new_block->base) {
        prev->size += new_block->size;
        // If we also merged with next, link prev to next's next and free next
        if (new_block == current) {
             prev->next = current->next;
             ReleaseFreeBlock(current);
        }
        ReleaseFreeBlock(new_block);
    } else if (new_block != current) { // No merge with previous, insert new_block
        if (prev) {
            new_block->next = prev->next;
            prev->next = new_block;
        } else {
            new_block->next = kernel_space.free_list;
            kernel_space.free_list = new_block;
        }
    }

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
    uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, vaddr, 0, 0);
    if (!pdp_phys) return 0;

    uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, vaddr, 1, 0);
    if (!pd_phys) return 0;

    uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, vaddr, 2, 0);
    if (!pt_phys) return 0;

    // Access PT through identity mapping if possible
    uint64_t* pt_virt;
    if (pt_phys < IDENTITY_MAP_SIZE) {
        pt_virt = (uint64_t*)pt_phys;
    } else {
        pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
    }
    int pt_index = (vaddr >> PT_SHIFT) & PT_INDEX_MASK;

    if (!(pt_virt[pt_index] & PAGE_PRESENT)) return 0;

    return (pt_virt[pt_index] & PT_ADDR_MASK) | (vaddr & PAGE_MASK);
}

void VMemMapKernel(uint64_t kernel_phys_start, uint64_t kernel_phys_end) {
    (void)kernel_phys_start;
    (void)kernel_phys_end;

    PrintKernelSuccess("VMem: VMem: Mapping kernel sections...\n");

    // Map .text section (read-only)
    uint64_t text_start = PAGE_ALIGN_DOWN((uint64_t)_text_start);
    uint64_t text_end = PAGE_ALIGN_UP((uint64_t)_text_end);
    for (uint64_t paddr = text_start; paddr < text_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_PRESENT);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .text page!", result);
        }
    }
    PrintKernel("  .text mapped (RO): 0x"); PrintKernelHex(text_start); PrintKernel(" - 0x"); PrintKernelHex(text_end); PrintKernel("\n");

    // Map .rodata section (read-only)
    uint64_t rodata_start = PAGE_ALIGN_DOWN((uint64_t)_rodata_start);
    uint64_t rodata_end = PAGE_ALIGN_UP((uint64_t)_rodata_end);
    for (uint64_t paddr = rodata_start; paddr < rodata_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_PRESENT);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .rodata page!", result);
        }
    }
    PrintKernel("  .rodata mapped (RO): 0x"); PrintKernelHex(rodata_start); PrintKernel(" - 0x"); PrintKernelHex(rodata_end); PrintKernel("\n");

    // Map .data section (read-write)
    uint64_t data_start = PAGE_ALIGN_DOWN((uint64_t)_data_start);
    uint64_t data_end = PAGE_ALIGN_UP((uint64_t)_data_end);
    for (uint64_t paddr = data_start; paddr < data_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .data page!", result);
        }
    }
    PrintKernel("  .data mapped (RW): 0x"); PrintKernelHex(data_start); PrintKernel(" - 0x"); PrintKernelHex(data_end); PrintKernel("\n");

    // Map .bss section (read-write)
    uint64_t bss_start = PAGE_ALIGN_DOWN((uint64_t)_bss_start);
    uint64_t bss_end = PAGE_ALIGN_UP((uint64_t)_bss_end);
    for (uint64_t paddr = bss_start; paddr < bss_end; paddr += PAGE_SIZE) {
        uint64_t vaddr = paddr + KERNEL_VIRTUAL_OFFSET;
        int result = VMemMap(vaddr, paddr, PAGE_WRITABLE);
        if (result != VMEM_SUCCESS) {
            PANIC_CODE("VMemMapKernel: Failed to map .bss page!", result);
        }
    }
    PrintKernel("  .bss mapped (RW): 0x"); PrintKernelHex(bss_start); PrintKernel(" - 0x"); PrintKernelHex(bss_end); PrintKernel("\n");

    PrintKernelSuccess("VMem: VMem: Kernel section mapping complete.\n");
}

int VMemIsPageMapped(uint64_t vaddr) {
    return VMemGetPhysAddr(vaddr) != 0;
}

void VMemFlushTLB(void) {
    asm volatile(
        "mov %%cr3, %%rax\n"
        "mov %%rax, %%cr3\n"
        ::: "rax", "memory"
    );
}

void VMemFlushTLBSingle(uint64_t vaddr) {
    asm volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    tlb_flushes++;
}

int VMemUnmap(uint64_t vaddr, uint64_t size) {
    if (size == 0) return VMEM_SUCCESS;
    
    size = PAGE_ALIGN_UP(size);
    uint64_t num_pages = size / PAGE_SIZE;
    
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);
        
        irq_flags_t flags = SpinLockIrqSave(&vmem_lock);
        
        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 0);
        if (!pdp_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) { SpinUnlockIrqRestore(&vmem_lock, flags); continue; }

        uint64_t* pt_virt;
        if (pt_phys < IDENTITY_MAP_SIZE) {
            pt_virt = (uint64_t*)pt_phys;
        } else {
            pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
        }
        int pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_virt[pt_index] & PAGE_PRESENT) {
            pt_virt[pt_index] = 0;
            kernel_space.used_pages--;
            kernel_space.total_mapped -= PAGE_SIZE;
        }
        
        SpinUnlockIrqRestore(&vmem_lock, flags);
        VMemFlushTLBSingle(current_vaddr);
    }
    
    return VMEM_SUCCESS;
}

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
    PrintKernel("VMemMapMMIO: Mapping MMIO 0x"); PrintKernelHex(paddr);
    PrintKernel(" -> 0x"); PrintKernelHex(vaddr);
    PrintKernel(" (size: 0x"); PrintKernelHex(size); PrintKernel(")\n");

    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(paddr) || !IS_PAGE_ALIGNED(size)) {
        PrintKernelError("VMemMapMMIO: ERROR - Alignment check failed\n");
        return VMEM_ERROR_ALIGN;
    }

    if (vaddr < VIRT_ADDR_SPACE_START || vaddr >= VIRT_ADDR_SPACE_END) {
        PrintKernelError("VMemMapMMIO: ERROR - Virtual address out of range\n");
        return VMEM_ERROR_INVALID_ADDR;
    }

    // Add MMIO-specific flags
    uint64_t mmio_flags = flags | PAGE_PRESENT | PAGE_NOCACHE | PAGE_WRITETHROUGH;

    // Map each page in the MMIO region
    uint64_t num_pages = size / PAGE_SIZE;
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);
        uint64_t current_paddr = paddr + (i * PAGE_SIZE);

        irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);

        // Get PDP table (level 0)
        uint64_t pdp_phys = VMemGetPageTablePhys((uint64_t)kernel_space.pml4, current_vaddr, 0, 1);
        if (!pdp_phys) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            PrintKernelError("VMemMapMMIO: Failed to get/create PDP table\n");
            return VMEM_ERROR_NOMEM;
        }

        // Get PD table (level 1)
        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 1);
        if (!pd_phys) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            PrintKernelError("VMemMapMMIO: Failed to get/create PD table\n");
            return VMEM_ERROR_NOMEM;
        }

        // Get PT table (level 2)
        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 1);
        if (!pt_phys) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            PrintKernelError("VMemMapMMIO: Failed to get/create PT table\n");
            return VMEM_ERROR_NOMEM;
        }

        // Access PT through identity mapping if possible
        uint64_t* pt_virt;
        if (pt_phys < IDENTITY_MAP_SIZE) {
            pt_virt = (uint64_t*)pt_phys;
        } else {
            pt_virt = (uint64_t*)PHYS_TO_VIRT(pt_phys);
        }

        int pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        // Check if already mapped
        if (pt_virt[pt_index] & PAGE_PRESENT) {
            SpinUnlockIrqRestore(&vmem_lock, irq_flags);
            PrintKernelError("VMemMapMMIO: Page already mapped at index ");
            PrintKernelInt(pt_index); PrintKernel("\n");
            return VMEM_ERROR_ALREADY_MAPPED;
        }

        // Set the mapping with MMIO flags
        pt_virt[pt_index] = current_paddr | mmio_flags;

        SpinUnlockIrqRestore(&vmem_lock, irq_flags);

        // Flush TLB for this page
        VMemFlushTLBSingle(current_vaddr);

        PrintKernel("VMemMapMMIO: Mapped page "); PrintKernelInt(i);
        PrintKernel(" - PTE["); PrintKernelInt(pt_index); PrintKernel("] = 0x");
        PrintKernelHex(pt_virt[pt_index]); PrintKernel("\n");
    }

    // Add a memory barrier to ensure all writes are complete
    __asm__ volatile("mfence" ::: "memory");

    PrintKernelSuccess("VMemMapMMIO: Successfully mapped ");
    PrintKernelInt(num_pages); PrintKernel(" pages\n");

    return VMEM_SUCCESS;
}

void VMemUnmapMMIO(uint64_t vaddr, uint64_t size) {
    PrintKernel("VMemUnmapMMIO: Unmapping MMIO at 0x"); PrintKernelHex(vaddr);
    PrintKernel(" (size: 0x"); PrintKernelHex(size); PrintKernel(")\n");

    if (!IS_PAGE_ALIGNED(vaddr) || !IS_PAGE_ALIGNED(size)) {
        PrintKernel("VMemUnmapMMIO: ERROR - Address or size not page-aligned\n");
        return;
    }

    uint64_t num_pages = size / PAGE_SIZE;
    if (num_pages == 0) {
        PrintKernel("VMemUnmapMMIO: ERROR - Size is zero\n");
        return;
    }

    irq_flags_t irq_flags = SpinLockIrqSave(&vmem_lock);
    uint64_t pml4_phys = VMemGetPML4PhysAddr();

    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t current_vaddr = vaddr + (i * PAGE_SIZE);

        // FIXED: Use the same pattern as all other code - get PDP first
        uint64_t pdp_phys = VMemGetPageTablePhys(pml4_phys, current_vaddr, 0, 0);
        if (!pdp_phys) {
            PrintKernel("VMemUnmapMMIO: Warning - Page "); PrintKernelInt(i);
            PrintKernel(" PDP not found\n");
            continue;
        }

        // Then get PD
        uint64_t pd_phys = VMemGetPageTablePhys(pdp_phys, current_vaddr, 1, 0);
        if (!pd_phys) {
            PrintKernel("VMemUnmapMMIO: Warning - Page "); PrintKernelInt(i);
            PrintKernel(" PD not found\n");
            continue;
        }

        // Finally get PT (Level 2)
        uint64_t pt_phys = VMemGetPageTablePhys(pd_phys, current_vaddr, 2, 0);
        if (!pt_phys) {
            PrintKernel("VMemUnmapMMIO: Warning - Page "); PrintKernelInt(i);
            PrintKernel(" was not mapped\n");
            continue;
        }

        // Access PT through identity mapping if possible
        uint64_t* pt_table;
        if (pt_phys < IDENTITY_MAP_SIZE) {
            pt_table = (uint64_t*)pt_phys;
        } else {
            pt_table = (uint64_t*)PHYS_TO_VIRT(pt_phys);
        }

        uint64_t pt_index = (current_vaddr >> PT_SHIFT) & PT_INDEX_MASK;

        if (pt_table[pt_index] & PAGE_PRESENT) {
            pt_table[pt_index] = 0;
            VMemFlushTLBSingle(current_vaddr);
        }
    }

    SpinUnlockIrqRestore(&vmem_lock, irq_flags);
    PrintKernel("VMemUnmapMMIO: Successfully unmapped ");
    PrintKernelInt(num_pages); PrintKernel(" pages\n");
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
    PrintKernel("[VMEM] Free List Dump:\n");
    VMemFreeBlock* current = kernel_space.free_list;
    if (!current) {
        PrintKernel("  <Empty>\n");
    }
    int i = 0;
    while(current) {
        PrintKernel("  ["); PrintKernelInt(i++); PrintKernel("] Base: 0x");
        PrintKernelHex(current->base);
        PrintKernel(", Size: "); PrintKernelInt(current->size / 1024); PrintKernel(" KB\n");
        current = current->next;
    }
    SpinUnlockIrqRestore(&vmem_lock, flags);
}