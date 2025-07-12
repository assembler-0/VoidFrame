#include "Panic.h"
#include "Kernel.h"
#include "Io.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

int ForceReboot() {
    PrintKernelError("[SYSTEM] Loading hardware reset modules...\n");
    asm volatile("lidt %0" :: "m"(*(short*)0));
    PrintKernelWarning("[SYSTEM] Rebooting now...");
    asm volatile("int $0x3");
    while (1) {
        __asm__ __volatile__("hlt");
    }
}


void KernelPanicHandler() {
    PrintKernelError("\n[SYSTEM] KernelPanicHandler() processing...\n");
    PrintKernelError("[SYSTEM] Found 1 solution(s)...\n");
    PrintKernelError("[SYSTEM] Forcing reboot, calling ForceReboot()\n");
    if (!ForceReboot()) {
        PrintKernelError("[SYSTEM] KernelPanicHandler() has encountered a fatal problem that it could not handled.");
        while (1) {
            asm volatile("hlt");
        }
    }
}

void __attribute__((noreturn)) Panic(const char* message) {
    asm volatile("cli");
    PrintKernelError("\n[SYSTEM] - [FATAL] - [----KERNEL PANIC----]\n");
    PrintKernelError(message);
    PrintKernelError("\n[SYSTEM] Calling KernelPanicHandler()...\n");
    KernelPanicHandler();
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");
    PrintKernelError("\np[SYSTEM] - [FATAL] - [----KERNEL PANIC----]\n");
    PrintKernelError(message);
    PrintKernelError("\n[SYSTEM] Error Code: ");
    PrintKernelHex(error_code);
    PrintKernelError(" -- Not handled");
    KernelPanicHandler();
    while (1) {
        __asm__ __volatile__("hlt");
    }
}