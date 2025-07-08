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
    
    char *vidptr = (char*)0xb8000;
    int offset = (CurrentLine * 80 + CurrentColumn) * 2;
    
    for (int k = 0; str[k] != '\0'; k++) {
        if (CurrentLine >= 25) break; // Screen bounds
        
        if (str[k] == '\n') {
            CurrentLine++;
            CurrentColumn = 0;
            offset = (CurrentLine * 80 + CurrentColumn) * 2;
        } else {
            if (offset < 80 * 25 * 2) {
                vidptr[offset] = str[k];
                vidptr[offset + 1] = 0x03;
            }
            offset += 2;
            CurrentColumn++;
            if (CurrentColumn >= 80) {
                CurrentLine++;
                CurrentColumn = 0;
            }
        }
    }
}

void PrintKernelHex(uint64_t num) {
    PrintKernel("0x");
    if (num == 0) {
        PrintKernel("0");
        return;
    }
    char buf[16];
    int i = 0;
    const char hex[] = "0123456789ABCDEF";

    while (num > 0 && i < 15) {
        buf[i++] = hex[num % 16];
        num /= 16;
    }
    buf[i] = '\0';
    // Reverse
    for (int j = 0; j < i/2; j++) {
        const char temp = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = temp;
    }
    PrintKernel(buf);
}


void PrintKernelInt(int num) {
    if (num == 0) {
        PrintKernel("0");
        return;
    }
    char buf[16];
    int i = 0;
    int negative = 0;

    if (num < 0) {
        negative = 1;
        num = -num;
    }
    // Extract digits in reverse
    while (num > 0 && i < 15) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    // Add negative sign
    if (negative) {
        buf[i++] = '-';
    }
    // Null terminate and reverse
    buf[i] = '\0';
    // Reverse the string
    for (int j = 0; j < i/2; j++) {
        char temp = buf[j];
        buf[j] = buf[i-1-j];
        buf[i-1-j] = temp;
    }
    PrintKernel(buf);
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

// Fast print - no bounds checking, direct memory write
static inline void FastPrint(const char *str, int line, int col) {
    uint16_t *vidptr = (uint16_t*)0xb8000;
    int pos = line * 80 + col;
    
    for (int i = 0; str[i]; i++) {
        vidptr[pos + i] = (0x03 << 8) | str[i]; // Combine char + color in one write
    }
}

// Fast print single character
static inline void FastPrintChar(char c, int line, int col) {
    uint16_t *vidptr = (uint16_t*)0xb8000;
    vidptr[line * 80 + col] = (0x03 << 8) | c;
}

// Fast print hex number
static inline void FastPrintHex(uint64_t num, int line, int col) {
    uint16_t *vidptr = (uint16_t*)0xb8000;
    int pos = line * 80 + col;
    
    vidptr[pos++] = (0x03 << 8) | '0';
    vidptr[pos++] = (0x03 << 8) | 'x';
    
    if (num == 0) {
        vidptr[pos] = (0x03 << 8) | '0';
        return;
    }
    
    char hex[] = "0123456789ABCDEF";
    char buf[16];
    int i = 0;
    
    while (num > 0) {
        buf[i++] = hex[num & 0xF];
        num >>= 4;
    }
    
    // Reverse and write
    while (i > 0) {
        vidptr[pos++] = (0x03 << 8) | buf[--i];
    }
}
void task1(void) {
    while (1) {
        FastPrint("T1 running", 10, 0);
        for(volatile int i = 0; i < 10000; i++); // Faster loop
    }
}

void task2(void) {
    while (1) {
        FastPrint("T2 running", 11, 0);
        for(volatile int i = 0; i < 10000; i++); // Faster loop
    }
}
void UserTask(void) {
    // This runs in Ring 3!
    asm volatile(
        "mov $2, %%rax\n"          // SYS_WRITE
        "mov $1, %%rbx\n"          // stdout
        "mov %0, %%rcx\n"          // message
        "mov $11, %%r8\n"          // length
        "int $0x80\n"
        :
        : "r"("User Ring 3!")
        : "rax", "rbx", "rcx", "r8"
    );
    
    while(1) {
        // User mode infinite loop
        for(volatile int i = 0; i < 100000; i++);
    }
}

void task3(void) {
    while (1) {
        FastPrint("T3 kernel", 12, 0);
        for(volatile int i = 0; i < 10000; i++); // Faster loop
    }
}
void KernelMain(uint32_t magic, uint32_t info) {
    ClearScreen();
    PrintKernel("VoidFrame Kernel - Version 0.0.1-alpha\n");
    PrintKernel("Initializing GDT...\n");
    GdtInit();
    PrintKernel("Initializing IDT...\n");
    IdtInstall();
    PrintKernel("Initializing System Calls...\n");
    SyscallInit();
    PrintKernel("Initializing PIC...\n");
    PicInstall();
    PrintKernel("Initializing Memory...\n");
    MemoryInit();
    PrintKernel("Initializing Processes...\n");
    ProcessInit();
    PrintKernel("Process system ready\n");
    CreateProcess(task1);
    CreateProcess(task2);
    CreateProcess(task3);
    CreateUserProcess(UserTask);  // Ring 3 process
    // Setup faster timer (PIT) - ~1000Hz instead of default 18.2Hz
    outb(0x43, 0x36);  // Command: channel 0, lobyte/hibyte, rate generator
    outb(0x40, 0xA9);  // Low byte of divisor (1193 = ~1000Hz)
    outb(0x40, 0x04);  // High byte of divisor
    
    // Enable timer interrupt (IRQ0)
    outb(0x21, inb(0x21) & ~0x01);

    // Enable all interrupts
    asm volatile("sti");

    // The kernel's idle loop with scheduler check
    while (1) {
        if (ShouldSchedule()) {
            Schedule();
        }
        asm volatile("hlt"); // Wait for the next interrupt
    }
}
