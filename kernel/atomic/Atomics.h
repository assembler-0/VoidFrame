//
// Created by Atheria on 7/12/25.
//
#include "stdint.h"
#ifndef ATOMICS_H
#define ATOMICS_H
void AtomicInc(volatile uint32_t* ptr);
void AtomicDec(volatile uint32_t* ptr);
int AtomicCmpxchg(volatile uint32_t* ptr, int expected, int desired);
uint32_t AtomicRead(volatile uint32_t* ptr);
#endif //ATOMICS_H
