/*
 * Kernel.c
 */
#include "Idt.h"
#include "Pic.h"
#include "Kernel.h"
#include "Memory.h"
#include "Process.h"
#include "Syscall.h"
#include "Gdt.h"
#include "UserMode.h"
#include "Io.h"
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
    char *vidptr = (char*)0xb8000;
    int offset = (CurrentLine * 80 + CurrentColumn) * 2;
    
    for (int k = 0; str[k] != '\0'; k++) {
        if (str[k] == '\n') {
            CurrentLine++;
            CurrentColumn = 0;
            offset = (CurrentLine * 80 + CurrentColumn) * 2;
        } else {
            vidptr[offset] = str[k];
            vidptr[offset + 1] = 0x03;
            offset += 2;
            CurrentColumn++;
            if (CurrentColumn >= 80) {
                CurrentLine++;
                CurrentColumn = 0;
            }
        }
    }
}

void PrintKernelHex(int num) {
    PrintKernel("0x");
    if (num == 0) {
        PrintKernel("0");
        return;
    }
    char buf[16];
    int i = 0;
    char hex[] = "0123456789ABCDEF";

    while (num > 0 && i < 15) {
        buf[i++] = hex[num % 16];
        num /= 16;
    }
    buf[i] = '\0';
    // Reverse
    for (int j = 0; j < i/2; j++) {
        char temp = buf[j];
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
    if (line >= 25) line = 24;
    char *vidptr = (char*)0xb8000;
    int offset = (line * 80 + col) * 2;
    for (int k = 0; str[k] != '\0'; k++) {
        vidptr[offset] = str[k];
        vidptr[offset + 1] = 0x03;
        offset += 2;
    }
}
void task1(void) {
    while (1) {
        PrintKernelAt("T1 running", 10, 0);
    }
    return;
}

void task2(void) {
    while (1) {
        PrintKernelAt("T2 running", 11, 0);
    }
    return;
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
        PrintKernelAt("T3 kernel", 12, 0);
        for(volatile int i = 0; i < 50000; i++);
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
    // Create your processes
    CreateProcess(task1);
    CreateProcess(task2);
    CreateProcess(task3);
    CreateUserProcess(UserTask);  // Ring 3 process
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
