//
// Created by Atheria on 7/12/25.
//

#include "Atomics.h"
void AtomicInc(volatile uint32_t* ptr) {
    asm volatile("lock incl %0" : "+m" (*ptr));
}
void AtomicDec(volatile uint32_t* ptr) {
    asm volatile("lock decl %0" : "+m" (*ptr));
}
int AtomicCmpxchg(volatile uint32_t* ptr, int expected, int desired) {
    int old;
    asm volatile(
        "lock cmpxchgl %2, %1"
        : "=a" (old), "+m" (*ptr)
        : "r" (desired), "0" (expected)
        : "memory"
    );
    return old;
}
uint32_t AtomicRead(volatile uint32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}
