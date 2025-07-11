#include "Panic.h"
#include "Kernel.h"
#include "Io.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

void __attribute__((noreturn)) KernelPanicHandler() {
    PrintKernelError("\nKernelPanicHandler() processing...\n\n");
    // No busy-wait, just halt
    PrintKernelError("KernelPanicHandler() has encountered a fatal problem that it could not handled.");
    while (1) {
        asm volatile("hlt");
    }
}

void __attribute__((noreturn)) Panic(const char* message) {
    asm volatile("cli");
    // ClearScreen();
    // CurrentLine = 0;
    // CurrentColumn = 0;
    // No busy-wait, just halt
    PrintKernelError("[FATAL] - [----KERNEL PANIC----]\n\n");
    PrintKernelError(message);
    PrintKernelError("\nCalling KernelPanicHandler()...\n");
    KernelPanicHandler();
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");
    // ClearScreen();
    // CurrentLine = 0;
    // CurrentColumn = 0;
    // No busy-wait, just halt
    PrintKernelError("[FATAL] - [----KERNEL PANIC----]\n");
    PrintKernelError(message);
    PrintKernelError("\nError Code: ");
    PrintKernelHex(error_code);
    PrintKernelError(" -- Not handled");
    KernelPanicHandler();
    while(1) {
        asm volatile("hlt");
    }
}