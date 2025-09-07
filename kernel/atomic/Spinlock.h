#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "Io.h"
#include "stdint.h"
#include "Panic.h"

#define DEADLOCK_TIMEOUT_CYCLES 100000000ULL
#define MAX_BACKOFF_CYCLES 1024

// Get CPU timestamp counter
static inline uint64_t get_cycles(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

// Exponential backoff delay
static inline void backoff_delay(uint64_t cycles) {
    uint64_t start = get_cycles();
    while (get_cycles() - start < cycles) {
        __builtin_ia32_pause();
    }
}

// Advanced spinlock with multiple anti-race mechanisms
static inline void SpinLock(volatile int* lock) {
    uint64_t start = get_cycles();
    uint64_t backoff = 1;
    uint32_t attempts = 0;

    while (1) {
        // Try to acquire without contention first
        if (!*lock && !__sync_lock_test_and_set(lock, 1)) {
            return;
        }

        // Deadlock detection
        if (get_cycles() - start > DEADLOCK_TIMEOUT_CYCLES) {
            backoff_delay(MAX_BACKOFF_CYCLES);
            start = get_cycles();
            attempts = 0;
            continue;
        }

        attempts++;

        // Adaptive spinning strategy
        if (attempts < 100) {
            // Initial fast spinning with pause
            for (int i = 0; i < 64; i++) {
                if (!*lock) break;
                __builtin_ia32_pause();
            }
        } else {
            // Switch to exponential backoff after many attempts
            backoff_delay(backoff);
            backoff = (backoff * 2) > MAX_BACKOFF_CYCLES ? MAX_BACKOFF_CYCLES : (backoff * 2);
        }
    }
}

// MCS-style queue lock (more fair, less cache bouncing)
typedef struct mcs_node {
    volatile struct mcs_node* next;
    volatile int locked;
} mcs_node_t;

static inline void MCSLock(volatile mcs_node_t** lock, mcs_node_t* node) {
    node->next = NULL;
    node->locked = 1;

    mcs_node_t* prev = (mcs_node_t*)__sync_lock_test_and_set(lock, node);
    if (prev) {
        prev->next = node;
        while (node->locked) __builtin_ia32_pause();
    }
}

static inline void MCSUnlock(volatile mcs_node_t** lock, mcs_node_t* node) {
    if (!node->next) {
        if (__sync_bool_compare_and_swap(lock, node, NULL)) {
            return;
        }
        while (!node->next) __builtin_ia32_pause();
    }
    node->next->locked = 0;
}

// Reader-Writer spinlock (if you need shared/exclusive access)
typedef struct {
    volatile int readers;
    volatile int writer;
    volatile uint32_t owner;
    volatile int recursion;
} rwlock_t;

#define RWLOCK_INIT { .readers = 0, .writer = 0, .owner = 0, .recursion = 0 }

static inline void ReadLock(rwlock_t* lock, uint32_t owner_id) {
    if (lock->writer && lock->owner == owner_id) {
        // The current process holds the write lock, so it can "read"
        return;
    }
    while (1) {
        while (lock->writer) __builtin_ia32_pause();
        __sync_fetch_and_add(&lock->readers, 1);
        if (!lock->writer) break;
        __sync_fetch_and_sub(&lock->readers, 1);
    }
}

static inline void ReadUnlock(rwlock_t* lock, uint32_t owner_id) {
    if (lock->writer && lock->owner == owner_id) {
        __atomic_thread_fence(__ATOMIC_RELEASE);
        return;
    }
    __sync_fetch_and_sub(&lock->readers, 1);
}

static inline void WriteLock(rwlock_t* lock, uint32_t owner_id) {
    if (lock->writer && lock->owner == owner_id) {
        lock->recursion++;
        return;
    }

    while (__sync_lock_test_and_set(&lock->writer, 1)) {
        while (lock->writer) __builtin_ia32_pause();
    }
    while (lock->readers) __builtin_ia32_pause();

    lock->owner = owner_id;
    lock->recursion = 1;
}

static inline void WriteUnlock(rwlock_t* lock) {
    if (lock->recursion <= 0) {
        lock->recursion = 0;
        lock->owner = 0;
        __sync_lock_release(&lock->writer);
        return;
    }
    if (--lock->recursion == 0) {
        lock->owner = 0;
        __sync_lock_release(&lock->writer);
    }
}

// Original API preserved
static inline void SpinUnlock(volatile int* lock) {
    __sync_lock_release(lock);
}

static inline irq_flags_t SpinLockIrqSave(volatile int* lock) {
    irq_flags_t flags = save_irq_flags();
    cli();
    SpinLock(lock);  // Uses the advanced version above
    return flags;
}

static inline void SpinUnlockIrqRestore(volatile int* lock, irq_flags_t flags) {
    __sync_lock_release(lock);
    restore_irq_flags(flags);
}

#endif // SPINLOCK_H