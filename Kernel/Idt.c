#include "Idt.h"

#define IDT_ENTRIES 256

struct IdtEntry g_Idt[IDT_ENTRIES];
struct IdtPtr   g_IdtPtr;

extern void IdtLoad(struct IdtPtr* idtPtr);

// Declare all ISRs from Interrupts.s
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

// IRQs
extern void isr32();
extern void isr33();
extern void isr34();
extern void isr35();
extern void isr36();
extern void isr37();
extern void isr38();
extern void isr39();
extern void isr40();
extern void isr41();
extern void isr42();
extern void isr43();
extern void isr44();
extern void isr45();
extern void isr46();
extern void isr47();

// // Generic ISRs (48-255)
// %for i in range(48, 256):
// extern void isr%i();
// %endfor

void IdtSetGate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    g_Idt[num].BaseLow = (base & 0xFFFF);
    g_Idt[num].Selector = sel;
    g_Idt[num].Reserved = 0;
    g_Idt[num].Flags = flags;
    g_Idt[num].BaseHigh = (base >> 16) & 0xFFFF;
}

void IdtInstall() {
    g_IdtPtr.Limit = (sizeof(struct IdtEntry) * IDT_ENTRIES) - 1;
    g_IdtPtr.Base  = (uint64_t)&g_Idt;

    // The code segment selector is 0x08 (from our GDT)
    uint16_t kernelCodeSegment = 0x08;
    uint8_t flags = 0x8E; // Present, DPL 0, 64-bit Interrupt Gate

    IdtSetGate(0, (uint64_t)isr0, kernelCodeSegment, flags);
    IdtSetGate(1, (uint64_t)isr1, kernelCodeSegment, flags);
    IdtSetGate(2, (uint64_t)isr2, kernelCodeSegment, flags);
    IdtSetGate(3, (uint64_t)isr3, kernelCodeSegment, flags);
    IdtSetGate(4, (uint64_t)isr4, kernelCodeSegment, flags);
    IdtSetGate(5, (uint64_t)isr5, kernelCodeSegment, flags);
    IdtSetGate(6, (uint64_t)isr6, kernelCodeSegment, flags);
    IdtSetGate(7, (uint64_t)isr7, kernelCodeSegment, flags);
    IdtSetGate(8, (uint64_t)isr8, kernelCodeSegment, flags);
    IdtSetGate(9, (uint64_t)isr9, kernelCodeSegment, flags);
    IdtSetGate(10, (uint64_t)isr10, kernelCodeSegment, flags);
    IdtSetGate(11, (uint64_t)isr11, kernelCodeSegment, flags);
    IdtSetGate(12, (uint64_t)isr12, kernelCodeSegment, flags);
    IdtSetGate(13, (uint64_t)isr13, kernelCodeSegment, flags);
    IdtSetGate(14, (uint64_t)isr14, kernelCodeSegment, flags);
    IdtSetGate(15, (uint64_t)isr15, kernelCodeSegment, flags);
    IdtSetGate(16, (uint64_t)isr16, kernelCodeSegment, flags);
    IdtSetGate(17, (uint64_t)isr17, kernelCodeSegment, flags);
    IdtSetGate(18, (uint64_t)isr18, kernelCodeSegment, flags);
    IdtSetGate(19, (uint64_t)isr19, kernelCodeSegment, flags);
    IdtSetGate(20, (uint64_t)isr20, kernelCodeSegment, flags);
    IdtSetGate(21, (uint64_t)isr21, kernelCodeSegment, flags);
    IdtSetGate(22, (uint64_t)isr22, kernelCodeSegment, flags);
    IdtSetGate(23, (uint64_t)isr23, kernelCodeSegment, flags);
    IdtSetGate(24, (uint64_t)isr24, kernelCodeSegment, flags);
    IdtSetGate(25, (uint64_t)isr25, kernelCodeSegment, flags);
    IdtSetGate(26, (uint64_t)isr26, kernelCodeSegment, flags);
    IdtSetGate(27, (uint64_t)isr27, kernelCodeSegment, flags);
    IdtSetGate(28, (uint64_t)isr28, kernelCodeSegment, flags);
    IdtSetGate(29, (uint64_t)isr29, kernelCodeSegment, flags);
    IdtSetGate(30, (uint64_t)isr30, kernelCodeSegment, flags);
    IdtSetGate(31, (uint64_t)isr31, kernelCodeSegment, flags);

    // IRQs
    IdtSetGate(32, (uint64_t)isr32, kernelCodeSegment, flags);
    IdtSetGate(33, (uint64_t)isr33, kernelCodeSegment, flags);
    IdtSetGate(34, (uint64_t)isr34, kernelCodeSegment, flags);
    IdtSetGate(35, (uint64_t)isr35, kernelCodeSegment, flags);
    IdtSetGate(36, (uint64_t)isr36, kernelCodeSegment, flags);
    IdtSetGate(37, (uint64_t)isr37, kernelCodeSegment, flags);
    IdtSetGate(38, (uint64_t)isr38, kernelCodeSegment, flags);
    IdtSetGate(39, (uint64_t)isr39, kernelCodeSegment, flags);
    IdtSetGate(40, (uint64_t)isr40, kernelCodeSegment, flags);
    IdtSetGate(41, (uint64_t)isr41, kernelCodeSegment, flags);
    IdtSetGate(42, (uint64_t)isr42, kernelCodeSegment, flags);
    IdtSetGate(43, (uint64_t)isr43, kernelCodeSegment, flags);
    IdtSetGate(44, (uint64_t)isr44, kernelCodeSegment, flags);
    IdtSetGate(45, (uint64_t)isr45, kernelCodeSegment, flags);
    IdtSetGate(46, (uint64_t)isr46, kernelCodeSegment, flags);
    IdtSetGate(47, (uint64_t)isr47, kernelCodeSegment, flags);

    // Generic ISRs (48-255)
    // %for i in range(48, 256):
    // IdtSetGate(%i, (uint64_t)isr%i, kernelCodeSegment, flags);
    // %endfor

    IdtLoad(&g_IdtPtr);
}
