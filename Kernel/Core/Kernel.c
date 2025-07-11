/*
 * Kernel.c - VoidFrame Kernel Main Module
 * Modern C implementation with optimizations
 */
#include "stdint.h"
#include "../System/Idt.h"
#include "../Drivers/Pic.h"
#include "Kernel.h"
#include "../Memory/Memory.h"
#include "../Process/Process.h"
#include "../System/Syscall.h"
#include "../System/Gdt.h"
#include "Panic.h"
#include "UserMode.h"
#include "../Drivers/Io.h"
#include "../Memory/VMem.h"

#define NULL ((void*)0)
#define true 1
#define false 0
typedef int bool;

// VGA Constants
#define VGA_BUFFER_ADDR     0xB8000
#define VGA_WIDTH           80
#define VGA_HEIGHT          25
#define VGA_BUFFER_SIZE     (VGA_WIDTH * VGA_HEIGHT)
#define VGA_COLOR_DEFAULT   0x08
#define VGA_COLOR_SUCCESS   0x0A
#define VGA_COLOR_ERROR     0x0C
#define VGA_COLOR_WARNING   0x0E

// Console state
typedef struct {
    uint32_t line;
    uint32_t column;
    volatile uint16_t* buffer;
    uint8_t color;
} ConsoleT;

typedef enum {
    INIT_SUCCESS = 0,
    INIT_ERROR_GDT,
    INIT_ERROR_IDT,
    INIT_ERROR_SYSCALL,
    INIT_ERROR_PIC,
    INIT_ERROR_MEMORY,
    INIT_ERROR_PROCESS,
    INIT_ERROR_SECURITY
} InitResultT;

static ConsoleT console = {
    .line = 0,
    .column = 0,
    .buffer = (volatile uint16_t*)VGA_BUFFER_ADDR,
    .color = VGA_COLOR_DEFAULT
};

// Inline functions for better performance
static inline void ConsoleSetColor(uint8_t color) {
    console.color = color;
}

static inline uint16_t MakeVGAEntry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static inline void ConsolePutcharAt(char c, uint32_t x, uint32_t y, uint8_t color) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    const uint32_t index = y * VGA_WIDTH + x;
    console.buffer[index] = MakeVGAEntry(c, color);
}

// Optimized screen clear using memset-like approach
void ClearScreen(void) {
    const uint16_t blank = MakeVGAEntry(' ', VGA_COLOR_DEFAULT);
    
    // Use 32-bit writes for better performance
    volatile uint32_t* buffer32 = (volatile uint32_t*)console.buffer;
    const uint32_t blank32 = ((uint32_t)blank << 16) | blank;
    const uint32_t size32 = (VGA_WIDTH * VGA_HEIGHT) / 2;
    
    for (uint32_t i = 0; i < size32; i++) {
        buffer32[i] = blank32;
    }
    
    console.line = 0;
    console.column = 0;
}

// Optimized scrolling
static void ConsoleScroll(void) {
    // Move all lines up by one using memmove-like operation
    for (uint32_t i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        console.buffer[i] = console.buffer[i + VGA_WIDTH];
    }
    
    // Clear the last line
    const uint16_t blank = MakeVGAEntry(' ', console.color);
    const uint32_t last_line_start = (VGA_HEIGHT - 1) * VGA_WIDTH;
    
    for (uint32_t i = 0; i < VGA_WIDTH; i++) {
        console.buffer[last_line_start + i] = blank;
    }
}

// Optimized character output with bounds checking
static void ConsolePutchar(char c) {
    if (c == '\n') {
        console.line++;
        console.column = 0;
    } else if (c == '\r') {
        console.column = 0;
    } else if (c == '\t') {
        console.column = (console.column + 8) & ~7; // Align to 8
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    } else if (c >= 32) { // Printable characters only
        ConsolePutcharAt(c, console.column, console.line, console.color);
        console.column++;
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    }
    
    // Handle scrolling
    if (console.line >= VGA_HEIGHT) {
        ConsoleScroll();
        console.line = VGA_HEIGHT - 1;
    }
}

// Modern string output with length checking
void PrintKernel(const char* str) {
    if (!str) return;
    
    // Cache the original color
    const uint8_t original_color = console.color;
    
    for (const char* p = str; *p; p++) {
        ConsolePutchar(*p);
    }
    
    console.color = original_color;
}

// Colored output variants
void PrintKernelSuccess(const char* str) {
    ConsoleSetColor(VGA_COLOR_SUCCESS);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelError(const char* str) {
    ConsoleSetColor(VGA_COLOR_ERROR);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelWarning(const char* str) {
    ConsoleSetColor(VGA_COLOR_WARNING);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

// Optimized hex printing with proper formatting
void PrintKernelHex(uint64_t num) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19]; // "0x" + 16 hex digits + null terminator
    
    buffer[0] = '0';
    buffer[1] = 'x';
    
    if (num == 0) {
        buffer[2] = '0';
        buffer[3] = '\0';
        PrintKernel(buffer);
        return;
    }
    
    int pos = 18;
    buffer[pos--] = '\0';
    
    while (num > 0 && pos >= 2) {
        buffer[pos--] = hex_chars[num & 0xF];
        num >>= 4;
    }
    
    PrintKernel(&buffer[pos + 1]);
}

// Optimized integer printing with proper sign handling
void PrintKernelInt(int64_t num) {
    char buffer[21]; // Max digits for 64-bit signed integer + sign + null
    
    if (num == 0) {
        PrintKernel("0");
        return;
    }
    
    bool negative = num < 0;
    if (negative) num = -num;
    
    int pos = 20;
    buffer[pos--] = '\0';
    
    while (num > 0 && pos >= 0) {
        buffer[pos--] = '0' + (num % 10);
        num /= 10;
    }
    
    if (negative && pos >= 0) {
        buffer[pos--] = '-';
    }
    
    PrintKernel(&buffer[pos + 1]);
}

// Safe positioned printing
void PrintKernelAt(const char* str, uint32_t line, uint32_t col) {
    if (!str || line >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    
    const uint32_t saved_line = console.line;
    const uint32_t saved_col = console.column;
    
    console.line = line;
    console.column = col;
    
    // Print until end of line or string
    for (const char* p = str; *p && console.column < VGA_WIDTH; p++) {
        if (*p == '\n') break;
        ConsolePutchar(*p);
    }
    
    // Restore cursor position
    console.line = saved_line;
    console.column = saved_col;
}

// Modern splash screen with better formatting
void AsciiSplash(void) {
    ClearScreen();
    
    const char* splash_lines[] = {
        "+-----------------------------------------------------------------------------+",
        "|                   >> VoidFrameKernel Version 0.0.1-alpha <<                 |",
        "|                                                                             |",
        "|    Copyright (C) 2025 VoidFrame Project - Atheria                           |",
        "|    Licensed under GNU General Public License v2.0                           |",
        "|                                                                             |",
        "|    This program is free software; you can redistribute it and/or modify     |",
        "|    it under the terms of the GNU General Public License as published by     |",
        "|    the Free Software Foundation; either version 2 of the License.           |",
        "|                                                                             |",
        "+-----------------------------------------------------------------------------+",
        "",
        NULL
    };
    
    ConsoleSetColor(VGA_COLOR_SUCCESS);
    for (int i = 0; splash_lines[i]; i++) {
        PrintKernel(splash_lines[i]);
        PrintKernel("\n");
    }
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}


static InitResultT SystemInitialize(void) {
    // Initialize GDT
    PrintKernel("[INFO] Initializing GDT...\n");
    GdtInit();  // void function - assume success
    PrintKernelSuccess("[KERNEL] GDT initialized\n");

    // Initialize IDT
    PrintKernel("[INFO] Initializing IDT...\n");
    IdtInstall();  // void function - assume success
    PrintKernelSuccess("[KERNEL] IDT initialized\n");

    // Initialize System Calls
    PrintKernel("[INFO] Initializing system calls...\n");
    SyscallInit();  // void function - assume success
    PrintKernelSuccess("[KERNEL] System calls initialized\n");

    // Initialize PIC
    PrintKernel("[INFO] Initializing PIC...\n");
    PicInstall();  // void function - assume success
    PrintKernelSuccess("[KERNEL] PIC initialized\n");

    // Initialize Memory Management
    PrintKernel("[INFO] Initializing memory management...\n");
    MemoryInit();  // void function - assume success
    PrintKernelSuccess("[KERNEL] Memory management initialized\n");

    // Initialize Process Management
    PrintKernel("[INFO] Initializing process management...\n");
    ProcessInit();  // void function - assume success
    PrintKernelSuccess("[KERNEL] Process management initialized\n");
    
    return INIT_SUCCESS;
}
void KernelMain(uint32_t magic, uint32_t info) {
    AsciiSplash();
    PrintKernelSuccess("[KERNEL] VoidFrame Kernel - Version 0.0.1-alpha loaded\n");
    PrintKernel("Magic: ");
    PrintKernelHex(magic);
    PrintKernel(", Info: ");
    PrintKernelHex(info);
    PrintKernel("\n\n");
    SystemInitialize();
    // Create the security manager process (PID 1)
    PrintKernel("[INFO] Creating security manager process...\n");
    uint64_t security_pid = CreateSecureProcess(SecureKernelIntegritySubsystem, PROC_PRIV_SYSTEM);
    if (!security_pid) {
        PrintKernelError("[FATAL] Cannot create SecureKernelIntegritySubsystem\n");
        Panic("Critical security failure - cannot create security manager");
    }
    PrintKernelSuccess("[KERNEL] Security manager created with PID: ");
    PrintKernelInt(security_pid);
    PrintKernel("\n");
    PrintKernelSuccess("[KERNEL] Core system modules loaded\n");
    PrintKernelSuccess("[KERNEL] Kernel initialization complete\n");
    PrintKernelSuccess("[SYSTEM] Transferring control to SecureKernelIntegritySubsystem...\n\n");

    // Enable interrupts
    asm volatile("sti");

    while (1) {
        if (ShouldSchedule()) {
            RequestSchedule();
        }
        asm volatile("hlt");
    }
}