#include "Cpu.h"
#include "Console.h"
#include "stdbool.h"

static CpuFeatures cpu_features = {0};

/**
 * @brief Initializes CPU features, detecting and enabling SSE and AVX.
 * This is a critical step. An OS must enable these features in control
 * registers before they can be used, or a #UD fault will occur.
 */
void CpuInit(void) {
    uint32_t eax, ebx, ecx, edx;

    // --- Step 1: Enable SSE/SSE2 in the CR4 Control Register ---
    // The OS must set CR4.OSFXSR (bit 9) and CR4.OSXMMEXCPT (bit 10)
    // to indicate that it supports FXSAVE/FXRSTOR and can handle SSE exceptions.
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // Set OSFXSR
    cr4 |= (1 << 10); // Set OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    PrintKernelSuccess("VMem: CPU: CR4 configured for SSE/SSE2.\n");

    // --- Step 2: Detect basic features and OSXSAVE support with CPUID ---
    // CPUID Leaf 1 provides basic feature flags.
    eax = 1;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));

    cpu_features.sse = (edx >> 25) & 1;
    cpu_features.sse2 = (edx >> 26) & 1;

    // Most importantly, check if the CPU supports OSXSAVE (bit 27 of ECX).
    // If this is not set, the OS is not allowed to set XCR0 to enable AVX.
    cpu_features.osxsave = (ecx >> 27) & 1;
    if (!cpu_features.osxsave) {
        PrintKernelWarning("VMem: CPU: OSXSAVE not supported. AVX will be disabled.\n");
        // We can still use SSE/SSE2, but AVX is impossible.
        cpu_features.avx = false;
        cpu_features.avx2 = false;
        return;
    }
    PrintKernelSuccess("VMem: CPU: OSXSAVE supported.\n");

    // --- Step 3: Enable AVX by setting the XCR0 Control Register ---
    // The OS must set bits 1 (SSE state) and 2 (AVX state) in XCR0.
    // This is done using the XSETBV instruction.
    uint64_t xcr0 = (1 << 1) | (1 << 2); // Enable SSE and AVX state saving
    __asm__ volatile("xsetbv" :: "c"(0), "a"((uint32_t)xcr0), "d"((uint32_t)(xcr0 >> 32)));
    PrintKernelSuccess("VMem: CPU: XCR0 configured for AVX.\n");

    // --- Step 4: Now that AVX is enabled, detect AVX and AVX2 features ---
    // CPUID Leaf 1, ECX bit 28 for AVX
    cpu_features.avx = (ecx >> 28) & 1;

    // CPUID Leaf 7, Sub-leaf 0, EBX bit 5 for AVX2
    eax = 7;
    uint32_t subleaf = 0; // Must set ECX to 0 for the main sub-leaf
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(subleaf), "=d"(edx));
    cpu_features.avx2 = (ebx >> 5) & 1;

    // --- Final Report ---
    PrintKernelF("VMem: CPU Features Initialized: SSE[%d] SSE2[%d] AVX[%d] AVX2[%d]\n",
        cpu_features.sse, cpu_features.sse2, cpu_features.avx, cpu_features.avx2);

    if (cpu_features.avx && !cpu_features.avx2) {
        PrintKernelWarning("VMem: CPU: AVX1 detected. Some optimizations may be slower.\n");
    }
}

CpuFeatures* GetCpuFeatures(void) {
    return &cpu_features;
}
