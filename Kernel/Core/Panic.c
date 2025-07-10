#include "Panic.h"
#include "Kernel.h"
#include "Io.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

void __attribute__((noreturn)) KernelPanicHandler() {
    PrintKernel("\nKernelPanicHandler() processing...\n\n");
    // No busy-wait, just halt
    PrintKernel("KernelPanicHandler() has encountered a fatal problem that it could not handled.");
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
    PrintKernel("[FATAL] - [----KERNEL PANIC----]\n\n");
    PrintKernel(message);
    PrintKernel("\nCalling KernelPanicHandler()...\n");
    KernelPanicHandler();
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");
    // ClearScreen();
    // CurrentLine = 0;
    // CurrentColumn = 0;
    // No busy-wait, just halt
    PrintKernel("[FATAL] - [----KERNEL PANIC----]\n");
    PrintKernel(message);
    PrintKernel("\nError Code: ");
    PrintKernelHex(error_code);
    PrintKernel(" -- Not handled");
    PrintKernel("\n\nSystem halted.\n");
    
    while(1) {
        asm volatile("hlt");
    }
}