#ifndef CPU_H
#define CPU_H

#include "stdint.h"
#include "stdbool.h"

typedef struct {
    bool sse;
    bool sse2;
    bool sse3;
    bool ssse3;
    bool sse41;
    bool sse42;
    bool bmi1;
    bool bmi2;
    bool fma;
    bool osxsave;
    bool avx;
    bool avx2;
    bool avx512f;
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

typedef struct {
    // General purpose registers
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;

    // Segment registers
    uint16_t cs, ds, es, fs, gs, ss;

    // Control registers
    uint64_t cr0, cr2, cr3, cr4;
    uint64_t cr8;  // Only in 64-bit mode

    // Debug registers
    uint64_t dr0, dr1, dr2, dr3, dr6, dr7;

    // MSRs (selected important ones)
    uint64_t efer, star, lstar, cstar, sfmask;
    uint64_t fs_base, gs_base, kernel_gs_base;
} RegistersDumpT;

void CpuInit(void);
CpuFeatures* GetCpuFeatures(void);

static inline uint64_t __attribute__((always_inline)) rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void __attribute__((always_inline)) __full_mem_prot_init(void) {
    __sync_synchronize();
    __asm__ volatile("mfence; sfence; lfence" ::: "memory");
}

static inline void __attribute__((always_inline)) __full_mem_prot_end(void) {
    __asm__ volatile("mfence; sfence; lfence" ::: "memory");
    __sync_synchronize();
    __builtin_ia32_serialize();
}

void DumpRegisters(RegistersDumpT* dump);
void PrintRegisters(const RegistersDumpT* dump);

#endif // CPU_H