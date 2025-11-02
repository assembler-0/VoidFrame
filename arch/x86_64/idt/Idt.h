#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// An entry in the IDT (64-bit)
struct IdtEntry {
    uint16_t BaseLow;
    uint16_t Selector;
    uint8_t  Reserved;
    uint8_t  Flags;
    uint16_t BaseHigh;
    uint32_t BaseUpper;
    uint32_t Reserved2;
} __attribute__((packed));

// A pointer to the IDT
struct IdtPtr {
    uint16_t Limit;
    uint64_t Base;
} __attribute__((packed));


int IdtInstall();
void IdtSetGate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
#endif