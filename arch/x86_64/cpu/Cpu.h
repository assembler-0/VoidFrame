#ifndef CPU_H
#define CPU_H

#include "stdint.h"
#include <stdbool.h> // For bool type

typedef struct {
    bool sse;
    bool sse2;
    bool osxsave; // Does the OS support XSAVE/XRSTOR? (Crucial for AVX)
    bool avx;
    bool avx2;
} CpuFeatures;

// DO NOT TOUCH THIS STRUCTURE - must match interrupt ASM stack layout
typedef struct Registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rsi, rdi, rdx, rcx, rbx, rax;
    uint64_t ds, es, fs, gs;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
} __attribute__((packed)) Registers;

void CpuInit(void);
CpuFeatures* GetCpuFeatures(void);

static inline uint64_t __attribute__((always_inline)) rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void __attribute__((always_inline)) delay(uint64_t cycles) {
    while (cycles--) __asm__ volatile ("nop");
}

#endif // CPU_H