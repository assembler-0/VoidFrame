/*
 * kernel.c
 */
#include "Idt.h"
#include "Kernel.h"
void ClearScreen(char * vidptr, unsigned int j){
    while(j < 80 * 25 * 2) {
        vidptr[j] = ' ';
        vidptr[j+1] = 0x03;
        j = j + 2;
    }
    return;
}
void PrintKernel(const char * str, char * vidptr, unsigned int j, unsigned int i){
    while(str[j] != '\0') {
        vidptr[i] = str[j];
        vidptr[i+1] = 0x03;
        ++j;
        i = i + 2;
    }
    return;
}
void KernelMain(void) {
    IdtInstall();
    const char *str = "VoidFrame loaded";
    char *vidptr = (char*)0xb8000;
    unsigned int i = 0;
    unsigned int j = 0;
    ClearScreen(vidptr, j);
    PrintKernel(str, vidptr, j, i);

    return;
}
