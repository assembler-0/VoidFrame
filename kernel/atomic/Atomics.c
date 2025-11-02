//
// Created by Atheria on 7/12/25.
//

#include <Atomics.h>

// Prefer GCC/Clang __atomic builtins for portability in freestanding kernels.

// 32-bit basic ops
void AtomicInc(volatile uint32_t* ptr) {
    __atomic_fetch_add(ptr, 1u, __ATOMIC_SEQ_CST);
}
void AtomicDec(volatile uint32_t* ptr) {
    __atomic_fetch_sub(ptr, 1u, __ATOMIC_SEQ_CST);
}
int AtomicCmpxchg(volatile uint32_t* ptr, int expected, int desired) {
    // Return the previous value like x86 cmpxchg
    return __sync_val_compare_and_swap((volatile int*)ptr, expected, desired);
}
uint32_t AtomicRead(volatile uint32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}

// 32-bit extended ops
uint32_t AtomicFetchAdd(volatile uint32_t* ptr, uint32_t val) {
    return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}
uint32_t AtomicFetchSub(volatile uint32_t* ptr, uint32_t val) {
    return __atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST);
}
uint32_t AtomicExchange(volatile uint32_t* ptr, uint32_t val) {
    return __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST);
}
void AtomicStore(volatile uint32_t* ptr, uint32_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
}

uint32_t AtomicReadRelaxed(volatile uint32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}
uint32_t AtomicReadAcquire(volatile uint32_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}
void AtomicStoreRelaxed(volatile uint32_t* ptr, uint32_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_RELAXED);
}
void AtomicStoreRelease(volatile uint32_t* ptr, uint32_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
}

bool AtomicBitTestAndSet(volatile uint32_t* ptr, unsigned bit) {
    uint32_t mask = 1u << (bit & 31u);
    uint32_t old = __atomic_fetch_or(ptr, mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}
bool AtomicBitTestAndClear(volatile uint32_t* ptr, unsigned bit) {
    uint32_t mask = 1u << (bit & 31u);
    uint32_t old = __atomic_fetch_and(ptr, ~mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}
uint32_t AtomicFetchOr(volatile uint32_t* ptr, uint32_t mask) {
    return __atomic_fetch_or(ptr, mask, __ATOMIC_SEQ_CST);
}
uint32_t AtomicFetchAnd(volatile uint32_t* ptr, uint32_t mask) {
    return __atomic_fetch_and(ptr, mask, __ATOMIC_SEQ_CST);
}
uint32_t AtomicFetchXor(volatile uint32_t* ptr, uint32_t mask) {
    return __atomic_fetch_xor(ptr, mask, __ATOMIC_SEQ_CST);
}

void AtomicThreadFenceAcquire(void) { __atomic_thread_fence(__ATOMIC_ACQUIRE); }
void AtomicThreadFenceRelease(void) { __atomic_thread_fence(__ATOMIC_RELEASE); }
void AtomicThreadFenceSeqCst(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }

// 64-bit counterparts
void AtomicInc64(volatile uint64_t* ptr) {
    __atomic_fetch_add(ptr, 1ull, __ATOMIC_SEQ_CST);
}
void AtomicDec64(volatile uint64_t* ptr) {
    __atomic_fetch_sub(ptr, 1ull, __ATOMIC_SEQ_CST);
}
uint64_t AtomicFetchAdd64(volatile uint64_t* ptr, uint64_t val) {
    return __atomic_fetch_add(ptr, val, __ATOMIC_SEQ_CST);
}
uint64_t AtomicFetchSub64(volatile uint64_t* ptr, uint64_t val) {
    return __atomic_fetch_sub(ptr, val, __ATOMIC_SEQ_CST);
}
uint64_t AtomicExchange64(volatile uint64_t* ptr, uint64_t val) {
    return __atomic_exchange_n(ptr, val, __ATOMIC_SEQ_CST);
}
int64_t AtomicCmpxchg64(volatile uint64_t* ptr, int64_t expected, int64_t desired) {
    return __sync_val_compare_and_swap((volatile long long*)ptr, expected, desired);
}
uint64_t AtomicRead64(volatile uint64_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_SEQ_CST);
}
void AtomicStore64(volatile uint64_t* ptr, uint64_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_SEQ_CST);
}
uint64_t AtomicReadRelaxed64(volatile uint64_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_RELAXED);
}
uint64_t AtomicReadAcquire64(volatile uint64_t* ptr) {
    return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}
void AtomicStoreRelaxed64(volatile uint64_t* ptr, uint64_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_RELAXED);
}
void AtomicStoreRelease64(volatile uint64_t* ptr, uint64_t val) {
    __atomic_store_n(ptr, val, __ATOMIC_RELEASE);
}
bool AtomicBitTestAndSet64(volatile uint64_t* ptr, unsigned bit) {
    uint64_t mask = 1ull << (bit & 63u);
    uint64_t old = __atomic_fetch_or(ptr, mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}
bool AtomicBitTestAndClear64(volatile uint64_t* ptr, unsigned bit) {
    uint64_t mask = 1ull << (bit & 63u);
    uint64_t old = __atomic_fetch_and(ptr, ~mask, __ATOMIC_SEQ_CST);
    return (old & mask) != 0;
}
uint64_t AtomicFetchOr64(volatile uint64_t* ptr, uint64_t mask) {
    return __atomic_fetch_or(ptr, mask, __ATOMIC_SEQ_CST);
}
uint64_t AtomicFetchAnd64(volatile uint64_t* ptr, uint64_t mask) {
    return __atomic_fetch_and(ptr, mask, __ATOMIC_SEQ_CST);
}
uint64_t AtomicFetchXor64(volatile uint64_t* ptr, uint64_t mask) {
    return __atomic_fetch_xor(ptr, mask, __ATOMIC_SEQ_CST);
}
