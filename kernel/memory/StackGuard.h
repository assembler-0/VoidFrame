#ifndef STACK_GUARD_H
#define STACK_GUARD_H

#include "stdint.h"
#include "Memory.h"
#include "Console.h"

#define STACK_CANARY_VALUE 0xDEADBEEFCAFEBABE

extern uint64_t __stack_chk_guard;

void __stack_chk_fail(void);
void StackGuardInit(void);

static inline void CheckResourceLeaks(void) {
    static uint64_t last_alloc_count   = 0;
    static uint64_t last_free_count    = 0;
    static uint64_t leak_check_counter = 0;
    static int      initialized        = 0;
    // Only sample every 100 calls to keep overhead low
    if (++leak_check_counter % 100 != 0) {
        return;
    }
    MemoryStats stats;
    GetDetailedMemoryStats(&stats);
    // Establish a baseline on first sample to avoid a guaranteed false positive
    if (!initialized) {
        last_alloc_count = stats.allocation_count;
        last_free_count  = stats.free_count;
        initialized      = 1;
        return;
    }
    uint64_t delta_alloc = stats.allocation_count - last_alloc_count;
    uint64_t delta_free  = stats.free_count      - last_free_count;
    uint64_t net_growth  = (delta_alloc > delta_free)
                            ? (delta_alloc - delta_free)
                            : 0;
    // Heuristic: warn on notable net growth over the last window
    if (net_growth > 64 && delta_alloc > (delta_free * 2)) {
        PrintKernelWarning("Potential memory leak detected: ");
        PrintKernel("net +");
        PrintKernelInt((uint32_t)net_growth);
        PrintKernel(" allocations since last check\n");
    }
    last_alloc_count  = stats.allocation_count;
    last_free_count   = stats.free_count;
}

#endif