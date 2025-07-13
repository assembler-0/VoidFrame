#ifndef CPU_H
#define CPU_H

#include "stdint.h"
typedef struct {
    uint8_t sse:1;
    uint8_t sse2:1;
    uint8_t avx:1;
    uint8_t avx2:1;
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
void EnableSse(void);

#endif