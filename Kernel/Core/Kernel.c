/*
 * Kernel.c
 */
#include "../System/Idt.h"
#include "../Drivers/Pic.h"
#include "Kernel.h"
#include "../Memory/Memory.h"
#include "../Process/Process.h"
#include "../System/Syscall.h"
#include "../System/Gdt.h"
#include "../Process/UserMode.h"
#include "../Drivers/Io.h"
#include "../Drivers/Driver.h"
#include "Shell.h"
#include "Panic.h"

extern void KeyboardRegister(void);
int CurrentLine = 0;
int CurrentColumn = 0;
void ClearScreen(){
    char *vidptr = (char*)0xb8000;
    for (int j = 0; j < 80 * 25 * 2; j += 2) {
        vidptr[j] = ' ';
        vidptr[j+1] = 0x03;
    }
}

void PrintKernel(const char *str){
    if (!str) return;
    uint16_t *vidptr = (uint16_t*)0xb8000;
    for (int k = 0; str[k] != '\0'; k++) {
        if (CurrentLine >= 25) break;
        
        if (str[k] == '\n') {
            CurrentLine++;
            CurrentColumn = 0;
        } else {
            int pos = CurrentLine * 80 + CurrentColumn;
            if (pos < 80 * 25) {
                vidptr[pos] = (0x03 << 8) | str[k]; // Fast 16-bit write
            }
            CurrentColumn++;
            if (CurrentColumn >= 80) {
                CurrentLine++;
                CurrentColumn = 0;
            }
        }
    }
}

void PrintKernelHex(uint64_t num) {
    uint16_t *vidptr = (uint16_t*)0xb8000;
    
    // Print "0x"
    if (CurrentLine < 25) {
        int pos = CurrentLine * 80 + CurrentColumn;
        if (pos < 80 * 25 - 1) {
            vidptr[pos] = (0x03 << 8) | '0';
            vidptr[pos + 1] = (0x03 << 8) | 'x';
            CurrentColumn += 2;
        }
    }
    
    if (num == 0) {
        if (CurrentLine < 25) {
            int pos = CurrentLine * 80 + CurrentColumn;
            if (pos < 80 * 25) {
                vidptr[pos] = (0x03 << 8) | '0';
                CurrentColumn++;
            }
        }
        return;
    }
    
    char buf[16];
    int i = 0;
    const char hex[] = "0123456789ABCDEF";
    
    while (num > 0 && i < 15) {
        buf[i++] = hex[num & 0xF];
        num >>= 4;
    }
    
    // Print reversed
    while (i > 0 && CurrentLine < 25) {
        int pos = CurrentLine * 80 + CurrentColumn;
        if (pos < 80 * 25) {
            vidptr[pos] = (0x03 << 8) | buf[--i];
            CurrentColumn++;
            if (CurrentColumn >= 80) {
                CurrentLine++;
                CurrentColumn = 0;
            }
        } else break;
    }
}


void PrintKernelInt(int num) {
    uint16_t *vidptr = (uint16_t*)0xb8000;
    
    if (num == 0) {
        if (CurrentLine < 25) {
            int pos = CurrentLine * 80 + CurrentColumn;
            if (pos < 80 * 25) {
                vidptr[pos] = (0x03 << 8) | '0';
                CurrentColumn++;
            }
        }
        return;
    }
    
    char buf[16];
    int i = 0;
    int negative = 0;
    
    if (num < 0) {
        negative = 1;
        num = -num;
    }
    
    while (num > 0 && i < 15) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    if (negative) {
        buf[i++] = '-';
    }
    
    // Print reversed
    while (i > 0 && CurrentLine < 25) {
        int pos = CurrentLine * 80 + CurrentColumn;
        if (pos < 80 * 25) {
            vidptr[pos] = (0x03 << 8) | buf[--i];
            CurrentColumn++;
            if (CurrentColumn >= 80) {
                CurrentLine++;
                CurrentColumn = 0;
            }
        } else break;
    }
}

void PrintKernelAt(const char *str, int line, int col) {
    if (!str) return;
    if (line < 0 || line >= 25) return;
    if (col < 0 || col >= 80) return;
    
    char *vidptr = (char*)0xb8000;
    int offset = (line * 80 + col) * 2;
    
    for (int k = 0; str[k] != '\0' && k < 80; k++) {
        if (offset >= 80 * 25 * 2) break;
        vidptr[offset] = str[k];
        vidptr[offset + 1] = 0x03;
        offset += 2;
    }
}


void KernelKeeper(void) { // keep the thing alive or else...
    while (1) {
        for(volatile int i = 0; i < 100; i++); // Tiny loop
    }
}
void KernelMain(uint32_t magic, uint32_t info) {
    
    ClearScreen();
    PrintKernel("[SUCCESS] VoidFrame Kernel - Version 0.0.1-alpha loaded\n");
    GdtInit();
    IdtInstall();
    SyscallInit();
    PicInstall();
    MemoryInit();
    ProcessInit();
    KeyboardRegister();
    DriverInit();
    PrintKernel("[SUCCESS] System modules loaded\n");
    ClearScreen();
    ShellInit();
    CreateProcess(KernelKeeper);
    outb(0x43, 0x36);  // Command: channel 0, lobyte/hibyte, rate generator
    outb(0x40, 0x4B);  // Low byte of divisor (299 = ~4000Hz)
    outb(0x40, 0x01);  // High byte of divisor
    outb(0x21, inb(0x21) & ~0x03);
    asm volatile("sti");
    while (1) {
        ShellRun();
        if (ShouldSchedule()) {
            Schedule();
        }
        asm volatile("hlt"); // Wait for the next interrupt
    }
}
