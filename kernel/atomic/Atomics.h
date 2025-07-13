//
// Created by Atheria on 7/12/25.
//
#include "stdint.h"
#ifndef ATOMICS_H
#define ATOMICS_H
void AtomicInc(volatile int* ptr);
void AtomicDec(volatile int* ptr);
int AtomicCmpxchg(volatile int* ptr, int expected, int desired);
uint32_t AtomicRead(volatile uint32_t* ptr);
#endif //ATOMICS_H
