#include "StackGuard.h"
#include "Panic.h"

uint64_t __stack_chk_guard = STACK_CANARY_VALUE;

void __stack_chk_fail(void) {
    PANIC("Stack overflow detected!");
}

void StackGuardInit(void) {
    // Use RDTSC for randomness
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    __stack_chk_guard ^= tsc;
}