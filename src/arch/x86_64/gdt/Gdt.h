#ifndef GDT_H
#define GDT_H

#include "stdint.h"

// GDT segment selectors
#define KERNEL_CODE_SELECTOR 0x08
#define KERNEL_DATA_SELECTOR 0x10
#define USER_CODE_SELECTOR   0x18
#define USER_DATA_SELECTOR   0x20

// GDT entry structure
struct GdtEntry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

// GDT pointer structure
struct GdtPtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

int GdtInit(void);

#endif