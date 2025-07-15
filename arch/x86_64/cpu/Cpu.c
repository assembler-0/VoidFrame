#include "Cpu.h"
#include "Panic.h"
static CpuFeatures cpu_features = {0};

static void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    ASSERT(eax != NULL && ebx != NULL && ecx != NULL && edx != NULL);
    asm volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf));
}

void CpuInit(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // Check for SSE/SSE2
    cpuid(1, &eax, &ebx, &ecx, &edx);
    cpu_features.sse = (edx >> 25) & 1;
    cpu_features.sse2 = (edx >> 26) & 1;
    cpu_features.avx = (ecx >> 28) & 1;
    
    // Check for AVX2
    cpuid(7, &eax, &ebx, &ecx, &edx);
    cpu_features.avx2 = (ebx >> 5) & 1;
    
    // SSE/SSE2 are already enabled by the bootloader (pxs.asm)
    // No need to call EnableSse() here.
}

void EnableSse(void) {
    asm volatile(
        "mov %%cr0, %%rax\n"
        "and $0xFFFB, %%ax\n"    // Clear CR0.EM
        "or $0x2, %%ax\n"        // Set CR0.MP
        "mov %%rax, %%cr0\n"
        "mov %%cr4, %%rax\n"
        "or $0x600, %%rax\n"     // Set CR4.OSFXSR and CR4.OSXMMEXCPT
        "mov %%rax, %%cr4\n"
        ::: "rax"
    );
}

CpuFeatures* GetCpuFeatures(void) {
    return &cpu_features;
}