#ifndef IO_H
#define IO_H

#include "stdint.h"
#include "x64.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

typedef uint64_t irq_flags_t;

static inline irq_flags_t save_irq_flags(void) {
    irq_flags_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags));
    return flags;
}

static inline void restore_irq_flags(irq_flags_t flags) {
    __asm__ volatile("pushq %0\n\tpopfq" : : "r"(flags));
}

static inline void __attribute__((always_inline, hot, flatten)) cli() {
    _full_mem_prot_start();
    __asm__ volatile("cli" ::: "memory");
#ifdef VF_CONFIG_INTEL
    _full_mem_prot_end_intel();
#else
    _full_mem_prot_end();
#endif
}

static inline void __attribute__((always_inline, hot, flatten)) sti() {
    _full_mem_prot_start();
    __asm__ volatile("sti" ::: "memory");
#ifdef VF_CONFIG_INTEL
    _full_mem_prot_end_intel();
#else
    _full_mem_prot_end();
#endif
}

// CPUID detection
static inline void __attribute__((always_inline)) cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf));
}

// MSR access
static inline __attribute__((always_inline)) uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline __attribute__((always_inline))  void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

#endif

