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
#include "Panic.h"


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
        if (str[k] == '\n') {
            CurrentLine++;
            CurrentColumn = 0;
        } else {
            int pos = CurrentLine * 80 + CurrentColumn;
            vidptr[pos] = (0x03 << 8) | str[k]; // Fast 16-bit write
            CurrentColumn++;
            if (CurrentColumn >= 80) {
                CurrentLine++;
                CurrentColumn = 0;
            }
        }
        // Handle scrolling
        if (CurrentLine >= 25) {
            // Move all lines up by one
            for (int i = 1; i < 25; i++) {
                for (int j = 0; j < 80; j++) {
                    vidptr[(i - 1) * 80 + j] = vidptr[i * 80 + j];
                }
            }
            // Clear the last line
            for (int j = 0; j < 80; j++) {
                vidptr[24 * 80 + j] = (0x03 << 8) | ' ';
            }
            CurrentLine = 24; // Keep cursor on the last line
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

void AsciiSplash() {
    ClearScreen();
    PrintKernel("+-----------------------------------------------------------------------------+\n");
    PrintKernel("|                   >> VoidFrameKernel Version 0.0.1-alpha <<                 |\n");
    PrintKernel("|                                                                             |\n");
    PrintKernel("|    Copyright (C) 2025 VoidFrame Project - Atheria                           |\n");
    PrintKernel("|    Licensed under GNU General Public License v2.0                           |\n");
    PrintKernel("|                                                                             |\n");
    PrintKernel("|    This program is free software; you can redistribute it and/or modify     |\n");
    PrintKernel("|    it under the terms of the GNU General Public License as published by     |\n");
    PrintKernel("|    the Free Software Foundation; either version 2 of the License.           |\n");
    PrintKernel("|                                                                             |\n");
    PrintKernel("+-----------------------------------------------------------------------------+\n\n");
}

void KernelMain(uint32_t magic, uint32_t info) {
    AsciiSplash();
    PrintKernel("[SUCCESS] VoidFrame Kernel - Version 0.0.1-alpha loaded\n");
    GdtInit();
    IdtInstall();
    SyscallInit();
    PicInstall();
    MemoryInit();
    ProcessInit();
    // Create the security manager process (PID 1) - this is critical
    uint32_t security_pid = CreateSecureProcess(SecureKernelIntegritySubsystem, PROC_PRIV_SYSTEM);
    if (!security_pid) {
        Panic("\nCannot create SecureKernelIntegritySubsystem() - Critical security failure\n");
    }

    PrintKernel("[SUCCESS] Security manager created with PID: ");
    PrintKernelInt(security_pid);
    PrintKernel("\n");

    PrintKernel("[SUCCESS] Core system modules loaded\n");
    PrintKernel("[SUCCESS] Kernel initialization complete.\n");
    PrintKernel("[SYSTEM] Handling control to SecureKernelIntegritySubsystem()...\n\n");
    asm volatile("sti");
    // Failsafe - Not used
    while (1) {
        if (ShouldSchedule()) {
            RequestSchedule();
        }
        asm volatile("hlt"); // Wait for the next interrupt
    }
}
