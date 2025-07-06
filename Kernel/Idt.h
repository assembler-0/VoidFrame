#ifndef IDT_H
#define IDT_H

#include "stdint.h"

// An entry in the IDT
struct IdtEntry {
    uint16_t BaseLow;
    uint16_t Selector;
    uint8_t  Reserved;
    uint8_t  Flags;
    uint16_t BaseHigh;
} __attribute__((packed));

// A pointer to the IDT
struct IdtPtr {
    uint16_t Limit;
    uint64_t Base;
} __attribute__((packed));


void IdtInstall();

#endif