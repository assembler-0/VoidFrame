//
// Created by Atheria on 7/12/25.
//
#include <stdint.h>
#include <stdbool.h>
#ifndef ATOMICS_H
#define ATOMICS_H

// 32-bit basic ops (existing)
void AtomicInc(volatile uint32_t* ptr);
void AtomicDec(volatile uint32_t* ptr);
int AtomicCmpxchg(volatile uint32_t* ptr, int expected, int desired); // returns previous value
uint32_t AtomicRead(volatile uint32_t* ptr); // seq_cst load

// 32-bit extended ops
uint32_t AtomicFetchAdd(volatile uint32_t* ptr, uint32_t val); // returns previous
uint32_t AtomicFetchSub(volatile uint32_t* ptr, uint32_t val); // returns previous
uint32_t AtomicExchange(volatile uint32_t* ptr, uint32_t val); // returns previous
void AtomicStore(volatile uint32_t* ptr, uint32_t val); // seq_cst store

// Memory order variants for 32-bit loads/stores
uint32_t AtomicReadRelaxed(volatile uint32_t* ptr);
uint32_t AtomicReadAcquire(volatile uint32_t* ptr);
void AtomicStoreRelaxed(volatile uint32_t* ptr, uint32_t val);
void AtomicStoreRelease(volatile uint32_t* ptr, uint32_t val);

// Bitwise ops (32-bit)
bool AtomicBitTestAndSet(volatile uint32_t* ptr, unsigned bit); // returns old bit
bool AtomicBitTestAndClear(volatile uint32_t* ptr, unsigned bit); // returns old bit
uint32_t AtomicFetchOr(volatile uint32_t* ptr, uint32_t mask);
uint32_t AtomicFetchAnd(volatile uint32_t* ptr, uint32_t mask);
uint32_t AtomicFetchXor(volatile uint32_t* ptr, uint32_t mask);

// Fences
void AtomicThreadFenceAcquire(void);
void AtomicThreadFenceRelease(void);
void AtomicThreadFenceSeqCst(void);

// 64-bit counterparts
void AtomicInc64(volatile uint64_t* ptr);
void AtomicDec64(volatile uint64_t* ptr);
uint64_t AtomicFetchAdd64(volatile uint64_t* ptr, uint64_t val);
uint64_t AtomicFetchSub64(volatile uint64_t* ptr, uint64_t val);
uint64_t AtomicExchange64(volatile uint64_t* ptr, uint64_t val);
int64_t AtomicCmpxchg64(volatile uint64_t* ptr, int64_t expected, int64_t desired); // returns previous
uint64_t AtomicRead64(volatile uint64_t* ptr);
void AtomicStore64(volatile uint64_t* ptr, uint64_t val);
uint64_t AtomicReadRelaxed64(volatile uint64_t* ptr);
uint64_t AtomicReadAcquire64(volatile uint64_t* ptr);
void AtomicStoreRelaxed64(volatile uint64_t* ptr, uint64_t val);
void AtomicStoreRelease64(volatile uint64_t* ptr, uint64_t val);
bool AtomicBitTestAndSet64(volatile uint64_t* ptr, unsigned bit);
bool AtomicBitTestAndClear64(volatile uint64_t* ptr, unsigned bit);
uint64_t AtomicFetchOr64(volatile uint64_t* ptr, uint64_t mask);
uint64_t AtomicFetchAnd64(volatile uint64_t* ptr, uint64_t mask);
uint64_t AtomicFetchXor64(volatile uint64_t* ptr, uint64_t mask);

#endif //ATOMICS_H
