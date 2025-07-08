#include "VMem.h"
#include "Memory.h"
#include "MemOps.h"

static VirtAddrSpace kernel_space;

void VMemInit(void) {
    kernel_space.pml4 = (uint64_t*)AllocPage();
    kernel_space.next_vaddr = VIRT_ADDR_SPACE_START;
    FastZeroPage(kernel_space.pml4);
}

static uint64_t* GetPageTable(uint64_t* pml4, uint64_t vaddr, int level, int create) {
    int index = (vaddr >> (39 - level * 9)) & 0x1FF;
    
    if (!(pml4[index] & PAGE_PRESENT)) {
        if (!create) return 0;
        uint64_t* new_table = (uint64_t*)AllocPage();
        if (!new_table) return 0;
        pml4[index] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    return (uint64_t*)(pml4[index] & ~0xFFF);
}

int VMemMap(uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t* pdp = GetPageTable(kernel_space.pml4, vaddr, 0, 1);
    if (!pdp) return -1;
    
    uint64_t* pd = GetPageTable(pdp, vaddr, 1, 1);
    if (!pd) return -1;
    
    uint64_t* pt = GetPageTable(pd, vaddr, 2, 1);
    if (!pt) return -1;
    
    int pt_index = (vaddr >> 12) & 0x1FF;
    pt[pt_index] = paddr | flags | PAGE_PRESENT;
    
    asm volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
    return 0;
}

void* VMemAlloc(uint64_t size) {
    size = (size + 4095) & ~4095; // Round up to page size
    uint64_t vaddr = kernel_space.next_vaddr;
    
    for (uint64_t offset = 0; offset < size; offset += 4096) {
        void* paddr = AllocPage();
        if (!paddr) return 0;
        
        if (VMemMap(vaddr + offset, (uint64_t)paddr, PAGE_WRITABLE) < 0) {
            return 0;
        }
    }
    
    kernel_space.next_vaddr += size;
    return (void*)vaddr;
}