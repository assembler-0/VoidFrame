#include "Gdt.h"
#include "Panic.h"

// GDT with 7 entries: null, kcode, kdata, ucode, udata, tss_low, tss_high
static struct GdtEntry gdt[7];
static struct GdtPtr gdt_ptr;
static struct TssEntry tss;

extern void GdtFlush(uint64_t gdt_ptr_addr);
extern void TssFlush(void);

static void SetGdtGate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;

    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access      = access;
}

// A cleaner way to set up the 64-bit TSS descriptor
static void SetTssGate(int num, uint64_t base, uint64_t limit) {
    // Set up the lower 8 bytes (standard system segment descriptor)
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].access      = GDT_ACCESS_TSS; // Access byte for 64-bit TSS
    gdt[num].granularity = (limit >> 16) & 0x0F; // No granularity bits (G=0, AVL=0)
    gdt[num].base_high   = (base >> 24) & 0xFF;

    // The second GDT entry for a 64-bit TSS holds the upper 32 bits of the base address.
    // We can cast the pointer to make this clearer.
    uint32_t* base_high_ptr = (uint32_t*)&gdt[num + 1];
    *base_high_ptr = (base >> 32);

    // The second 4 bytes of the second entry should be zero.
    // If gdt is a global static, it's already zero-initialized, but being explicit is good.
    *((uint32_t*)base_high_ptr + 1) = 0;
}


void GdtInit(void) {
    // The GDT limit is the size of the table in bytes, minus one.
    // We now have 7 entries.
    gdt_ptr.limit = (sizeof(struct GdtEntry) * 7) - 1;
    gdt_ptr.base  = (uint64_t)&gdt;

    SetGdtGate(0, 0, 0, 0, 0);                // 0x00: Null segment
    SetGdtGate(1, 0, 0xFFFFFFFF, GDT_ACCESS_CODE_PL0, GDT_GRAN_CODE); // 0x08: Kernel Code
    SetGdtGate(2, 0, 0xFFFFFFFF, GDT_ACCESS_DATA_PL0, GDT_GRAN_DATA); // 0x10: Kernel Data
    SetGdtGate(3, 0, 0xFFFFFFFF, GDT_ACCESS_CODE_PL3, GDT_GRAN_CODE); // 0x18: User Code
    SetGdtGate(4, 0, 0xFFFFFFFF, GDT_ACCESS_DATA_PL3, GDT_GRAN_DATA); // 0x20: User Data

    // Setup TSS. It starts at index 5 and occupies entries 5 and 6.
    // The selector will be 0x28 (5 * 8)
    uint64_t tss_base = (uint64_t)&tss;
    uint64_t tss_limit = sizeof(struct TssEntry) - 1;
    SetTssGate(5, tss_base, tss_limit);

    // Initialize TSS fields (make sure tss is zero-initialized)
    // tss.rsp0 will be set later when a process is created
    tss.iomap_base = sizeof(struct TssEntry); // Set IOMAP base beyond the TSS limit to disable it.

    GdtFlush((uint64_t)&gdt_ptr);
    TssFlush(); // Load the Task Register with selector 0x28
}

void SetTssRsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}