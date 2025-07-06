/*
 * Kernel.c
 */
#include "Idt.h"
#include "Pic.h"
#include "Kernel.h"
#include "Memory.h"
#include "Process.h"

void ClearScreen(){
    char *vidptr = (char*)0xb8000;
    for (int j = 0; j < 80 * 25 * 2; j += 2) {
        vidptr[j] = ' ';
        vidptr[j+1] = 0x03;
    }
}

void PrintKernel(const char *str, int line, int col){
    char *vidptr = (char*)0xb8000;
    int offset = (line * 80 + col) * 2;
    for (int k = 0; str[k] != '\0'; k++) {
        vidptr[offset] = str[k];
        vidptr[offset + 1] = 0x03;
        offset += 2;
    }
}

void task1(void) {
    PrintKernel("Task 1 running", 6, 0);
}

void task2(void) {
    int counter = 0;
    while (1) {
        PrintKernel("Task 2 running", 7, 0);
        for (volatile int i = 0; i < 10000000; i++); // Simple delay
        counter++;
    }
}

void KernelMain(void) {
    ClearScreen();
    PrintKernel("VoidFrame Kernel - Version 0.0.1-alpha", 0, 0);
    
    PrintKernel("Initializing IDT...", 1, 0);
    IdtInstall();
    
    PrintKernel("Initializing PIC...", 2, 0);
    PicInstall();
    
    PrintKernel("Initializing Memory...", 3, 0);
    MemoryInit();
    
    PrintKernel("Initializing Processes...", 4, 0);
    ProcessInit();

    PrintKernel("Process system ready", 5, 0);
    CreateProcess(task1);
    CreateProcess(task2);

    asm volatile("sti");
    while (1) {
        asm volatile("hlt");
    }
}
