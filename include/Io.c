#include "Io.h"
#include "x64.h"

void cli() {
    _full_mem_prot_start();
    __asm__ volatile("cli" ::: "memory");
#ifdef VF_CONFIG_INTEL
    _full_mem_prot_end_intel();
#else
    _full_mem_prot_end();
#endif
}

void sti() {
    _full_mem_prot_start();
    __asm__ volatile("sti" ::: "memory");
#ifdef VF_CONFIG_INTEL
    _full_mem_prot_end_intel();
#else
    _full_mem_prot_end();
#endif
}

// CPUID detection
void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf));
}

// MSR access
uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

irq_flags_t save_irq_flags(void) {
    irq_flags_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags));
    return flags;
}

void restore_irq_flags(irq_flags_t flags) {
    __asm__ volatile("pushq %0\n\tpopfq" : : "r"(flags));
}