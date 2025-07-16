#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "stdint.h"
#include "Io.h"

// Locks
static inline void SpinLock(volatile int* lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) __builtin_ia32_pause();
    }
}

static inline void SpinUnlock(volatile int* lock) {
    __sync_lock_release(lock);
}

// Spinlock with interrupt saving/restoring
static inline irq_flags_t SpinLockIrqSave(volatile int* lock) {
    irq_flags_t flags = save_irq_flags();
    cli();
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) __builtin_ia32_pause();
    }
    return flags;
}

static inline void SpinUnlockIrqRestore(volatile int* lock, irq_flags_t flags) {
    __sync_lock_release(lock);
    restore_irq_flags(flags);
}

#endif // SPINLOCK_H
