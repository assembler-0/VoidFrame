#include "x64.h"
#include "Console.h"
#include "Io.h"

static CpuFeatures cpu_features = {0};

static void CPUFeatureValidation(void) {
    uint32_t eax, ebx, ecx, edx;

    // Check for standard features (EAX=1)
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));

    cpu_features.sse3 = (ecx & (1 << 0)) != 0;
    cpu_features.ssse3 = (ecx & (1 << 9)) != 0;
    cpu_features.sse41 = (ecx & (1 << 19)) != 0;
    cpu_features.sse42 = (ecx & (1 << 20)) != 0;

    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(7), "c"(0));

    cpu_features.bmi1 = (ebx & (1 << 3)) != 0;
    cpu_features.bmi2 = (ebx & (1 << 8)) != 0;
    // FMA (FMA3) is CPUID.(EAX=1):ECX[12]
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    cpu_features.fma = (ecx & (1 << 12)) != 0;
}

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
#ifndef VF_CONFIG_VM_HOST // for some reason, #UD occurs even if with -cpu max
    cr4 |= (1 << 18); // Set OSXSAVE
#endif
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    PrintKernelSuccess("System: CPU: CR4 configured for SSE/SSE2.\n");

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
        PrintKernelWarning("System: CPU: OSXSAVE not supported. AVX will be disabled.\n");
        cpu_features.avx = false;
        cpu_features.avx2 = false;
        CPUFeatureValidation();
        return;
    }
    PrintKernelSuccess("System: CPU: OSXSAVE supported.\n");

    // --- Step 3: Enable AVX by setting the XCR0 Control Register ---
    // The OS must set bits 1 (SSE state) and 2 (AVX state) in XCR0.
    // This is done using the XSETBV instruction.
    uint64_t xcr0 = (1 << 1) | (1 << 2); // Enable SSE and AVX state saving
    __asm__ volatile("xsetbv" :: "c"(0), "a"((uint32_t)xcr0), "d"((uint32_t)(xcr0 >> 32)));
    PrintKernelSuccess("System: CPU: XCR0 configured for AVX.\n");

    // --- Step 4: Now that AVX is enabled, detect AVX and AVX2 features ---
    // CPUID Leaf 1, ECX bit 28 for AVX
    cpu_features.avx = (ecx >> 28) & 1;

    // CPUID Leaf 7, Sub-leaf 0, EBX bit 5 for AVX2
    eax = 7;
    uint32_t subleaf = 0; // Must set ECX to 0 for the main sub-leaf
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(subleaf), "=d"(edx));
    cpu_features.avx2 = (ebx >> 5) & 1;

    // --- Final Report ---
    PrintKernelF("System: CPU Features Initialized: SSE[%d] SSE2[%d] AVX[%d] AVX2[%d]\n",
        cpu_features.sse, cpu_features.sse2, cpu_features.avx, cpu_features.avx2);

    if (cpu_features.avx && !cpu_features.avx2) {
        PrintKernelWarning("System: CPU: AVX1 detected. Some optimizations may be slower.\n");
    }
    CPUFeatureValidation();
}

CpuFeatures* GetCpuFeatures(void) {
    return &cpu_features;
}

static void DumpGP(RegistersDumpT* dump) {
    // General purpose (tricky - we're using some of these!)
    asm volatile (
        "movq %%rax, %0\n"
        "movq %%rbx, %1\n" 
        "movq %%rcx, %2\n"
        "movq %%rdx, %3\n"
        "movq %%rsi, %4\n"
        "movq %%rdi, %5\n"
        "movq %%rbp, %6\n"
        "movq %%rsp, %7\n"
        "movq %%r8, %8\n"
        "movq %%r9, %9\n"
        "movq %%r10, %10\n"
        "movq %%r11, %11\n"
        "movq %%r12, %12\n"
        "movq %%r13, %13\n"
        "movq %%r14, %14\n"
        "movq %%r15, %15\n"
        : "=m"(dump->rax), "=m"(dump->rbx), "=m"(dump->rcx), "=m"(dump->rdx),
          "=m"(dump->rsi), "=m"(dump->rdi), "=m"(dump->rbp), "=m"(dump->rsp),
          "=m"(dump->r8), "=m"(dump->r9), "=m"(dump->r10), "=m"(dump->r11),
          "=m"(dump->r12), "=m"(dump->r13), "=m"(dump->r14), "=m"(dump->r15)
        :
        : "memory"
    );
    
    // Get RIP (tricky - need to use a call trick)
    asm volatile (
        "call 1f\n"
        "1: popq %0\n"
        : "=r"(dump->rip)
    );
    
    // Get RFLAGS
    asm volatile (
        "pushfq\n"
        "popq %0\n"
        : "=r"(dump->rflags)
    );
    
    // Segment registers
    asm volatile (
        "movw %%cs, %0\n"
        "movw %%ds, %1\n"
        "movw %%es, %2\n"
        "movw %%fs, %3\n"
        "movw %%gs, %4\n"
        "movw %%ss, %5\n"
        : "=m"(dump->cs), "=m"(dump->ds), "=m"(dump->es),
          "=m"(dump->fs), "=m"(dump->gs), "=m"(dump->ss)
    );
}

// Control registers (DANGEROUS - save/restore interrupts)
static void DumpCR(RegistersDumpT* dump) {
    unsigned long flags = save_irq_flags();
    cli();  // Critical section
    
    asm volatile ("movq %%cr0, %0" : "=r"(dump->cr0));
    asm volatile ("movq %%cr2, %0" : "=r"(dump->cr2));  // Page fault address
    asm volatile ("movq %%cr3, %0" : "=r"(dump->cr3));  // Page directory
    asm volatile ("movq %%cr4, %0" : "=r"(dump->cr4));
    
    // CR8 (TPR - Task Priority Register) only in x64
    asm volatile ("movq %%cr8, %0" : "=r"(dump->cr8));
    
    restore_irq_flags(flags);
}

// Debug registers
static void DumpDR(RegistersDumpT* dump) {
    asm volatile ("movq %%dr0, %0" : "=r"(dump->dr0));
    asm volatile ("movq %%dr1, %0" : "=r"(dump->dr1)); 
    asm volatile ("movq %%dr2, %0" : "=r"(dump->dr2));
    asm volatile ("movq %%dr3, %0" : "=r"(dump->dr3));
    asm volatile ("movq %%dr6, %0" : "=r"(dump->dr6));  // Debug status
    asm volatile ("movq %%dr7, %0" : "=r"(dump->dr7));  // Debug control
}

// MSRs (Model Specific Registers)
static void DumpMSR(RegistersDumpT* dump) {
    dump->efer = rdmsr(0xC0000080);           // Extended features
    dump->star = rdmsr(0xC0000081);           // Syscall target
    dump->lstar = rdmsr(0xC0000082);          // Long mode syscall target  
    dump->cstar = rdmsr(0xC0000083);          // Compat mode syscall
    dump->sfmask = rdmsr(0xC0000084);         // Syscall flag mask
    dump->fs_base = rdmsr(0xC0000100);        // FS base
    dump->gs_base = rdmsr(0xC0000101);        // GS base  
    dump->kernel_gs_base = rdmsr(0xC0000102); // Kernel GS base
}

void DumpRegisters(RegistersDumpT* dump) {
    DumpGP(dump);
    DumpCR(dump);
    DumpDR(dump);
    DumpMSR(dump);
}

void PrintRegisters(const RegistersDumpT* dump) {
    PrintKernelF("=== VoidFrame registers dump x64 ===\n");
    PrintKernelF("RAX: 0x%016lx  RBX: 0x%016lx\n", dump->rax, dump->rbx);
    PrintKernelF("RCX: 0x%016lx  RDX: 0x%016lx\n", dump->rcx, dump->rdx);
    PrintKernelF("RSI: 0x%016lx  RDI: 0x%016lx\n", dump->rsi, dump->rdi);
    PrintKernelF("RBP: 0x%016lx  RSP: 0x%016lx\n", dump->rbp, dump->rsp);
    PrintKernelF("R8:  0x%016lx  R9:  0x%016lx\n", dump->r8, dump->r9);
    PrintKernelF("R10: 0x%016lx  R11: 0x%016lx\n", dump->r10, dump->r11);
    PrintKernelF("R12: 0x%016lx  R13: 0x%016lx\n", dump->r12, dump->r13);
    PrintKernelF("R14: 0x%016lx  R15: 0x%016lx\n", dump->r14, dump->r15);
    PrintKernelF("RIP: 0x%016lx  CR0: 0x%016lx\n", dump->rip, dump->cr0);
    PrintKernelF("CR2: 0x%016lx  CR3: 0x%016lx\n", dump->cr2, dump->cr3);
    PrintKernelF("CR4: 0x%016lx  CR8: 0x%016lx\n", dump->cr4, dump->cr8);
    PrintKernelF("DR0: 0x%016lx  DR1: 0x%016lx\n", dump->dr0, dump->dr1);
    PrintKernelF("DR2: 0x%016lx  DR3: 0x%016lx\n", dump->dr2, dump->dr3);
    PrintKernelF("DR6: 0x%016lx  DR7: 0x%016lx\n", dump->dr6, dump->dr7);
    PrintKernelF("FS:  0x%016lx  GS:  0x%016lx\n", dump->fs, dump->gs);
    PrintKernelF("ES:  0x%016lx  DS:  0x%016lx\n", dump->es, dump->ds);
    PrintKernelF("SS:  0x%016lx  CS:  0x%016lx\n", dump->ss, dump->cs);
    PrintKernelF("EFER:  0x%016lx  STAR:  0x%016lx\n", dump->efer, dump->star);
    PrintKernelF("LSTAR: 0x%016lx  CSTAR: 0x%016lx\n", dump->lstar, dump->cstar);
    PrintKernelF("SFMASK:0x%016lx  KGSBASE:0x%016lx\n", dump->sfmask, dump->kernel_gs_base);
    PrintKernelF("FSBASE:0x%016lx  GSBASE:0x%016lx\n", dump->fs_base, dump->gs_base);
}
