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

static inline uint64_t ReadCr3(void) {
    uint64_t cr3_val;
    asm volatile("mov %%cr3, %0" : "=r"(cr3_val));
    return cr3_val;
}

void CpuInit(void);
CpuFeatures* GetCpuFeatures(void);

#endif // CPU_H