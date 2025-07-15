#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "stdint.h"

// Locks
static inline void SpinLock(volatile int* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) __builtin_ia32_pause();
    }
}

static inline void SpinUnlock(volatile int* lock) {
    __sync_lock_release(lock);
}

#endif // SPINLOCK_H
