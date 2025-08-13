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
    static uint64_t last_alloc_count = 0;
    static uint64_t leak_check_counter = 0;

    MemoryStats stats;
    GetDetailedMemoryStats(&stats);

    if (++leak_check_counter % 100 == 0) {  // Check every 100 calls
        if (stats.allocation_count > last_alloc_count * 2) {
            PrintKernelWarning("Potential memory leak detected\n");
        }
        last_alloc_count = stats.allocation_count;
    }
}

#endif