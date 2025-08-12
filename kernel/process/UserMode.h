#ifndef USERMODE_H
#define USERMODE_H

#include "VMem.h"
#include "stdbool.h"
#include "stdint.h"

static inline bool is_user_address(const void* ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;
    // Check for null pointer, non-canonical addresses, and kernel addresses
    if (addr == 0 || addr >= KERNEL_VIRTUAL_BASE) {
        return false;
    }
    // Check for overflow when adding size
    if (addr + size < addr) {
        return false;
    }
    // Check if the range crosses into kernel space
    if (addr + size >= KERNEL_VIRTUAL_BASE) {
        return false;
    }
    return true;
}

#endif