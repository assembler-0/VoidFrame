#include "Gdt.h"
#include "Panic.h"
// GDT with user mode segments
static struct GdtEntry gdt[5];
static struct GdtPtr gdt_ptr;

extern void GdtFlush(uint64_t gdt_ptr_addr);

static void SetGdtGate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

int GdtInit(void) {
    gdt_ptr.limit = (sizeof(struct GdtEntry) * 5) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;
    
    SetGdtGate(0, 0, 0, 0, 0);                // Null segment
    SetGdtGate(1, 0, 0xFFFFFFFF, 0x9A, 0xA0); // Kernel code (64-bit)
    SetGdtGate(2, 0, 0xFFFFFFFF, 0x92, 0xA0); // Kernel data
    SetGdtGate(3, 0, 0xFFFFFFFF, 0xFA, 0xA0); // User code (Ring 3, 64-bit)
    SetGdtGate(4, 0, 0xFFFFFFFF, 0xF2, 0xA0); // User data (Ring 3)
    
    GdtFlush((uint64_t)&gdt_ptr);
    return 0;
}