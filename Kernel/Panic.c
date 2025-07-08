#include "Panic.h"
#include "Kernel.h"
#include "Io.h"

void __attribute__((noreturn)) Panic(const char* message) {

    asm volatile("cli");

    ClearScreen();
    CurrentLine = 0;
    CurrentColumn = 0;
    
    PrintKernel("[----KERNEL PANIC----]\n");
    PrintKernel(message);
    PrintKernel("\n\nSystem halted.\n");

    while(1) {
        asm volatile("hlt");
    }
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");
    
    ClearScreen();
    CurrentLine = 0;
    CurrentColumn = 0;
    
    PrintKernel("[----KERNEL PANIC----]\n");
    PrintKernel(message);
    PrintKernel("\nError Code: ");
    PrintKernelHex(error_code);
    PrintKernel(" -- Not handled");
    PrintKernel("\n\nSystem halted.\n");
    
    while(1) {
        asm volatile("hlt");
    }
}