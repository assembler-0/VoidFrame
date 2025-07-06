/*
 * Kernel.c
 */
#include "Idt.h"
#include "Pic.h"
#include "Kernel.h"

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

void KernelMain(void) {
    IdtInstall();
    PicInstall();
    ClearScreen();
    PrintKernel("VoidFrame Kernel - Version 0.0.1-alpha", 0, 0);
    asm volatile("sti"); // Enable interrupts
    while (1){
        asm volatile("hlt");
    }
}
