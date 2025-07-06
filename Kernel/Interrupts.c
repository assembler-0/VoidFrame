#include "stdint.h"
#include "Kernel.h"
#include "Io.h"
#include "Process.h"

// Structure to save CPU registers
// This order must match the push order in Interrupts.asm
struct Registers {
    uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// Static counter for timer ticks
static uint64_t tick_count = 0;

// Function to convert a number to string (reused from previous code)
void itoa(uint64_t num, char* str) {
    int i = 0;
    if (num == 0) {
        str[i++] = '0';
    } else {
        while (num > 0) {
            str[i++] = (num % 10) + '0';
            num /= 10;
        }
    }
    str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char t = str[start];
        str[start] = str[end];
        str[end] = t;
        start++;
        end--;
    }
}

// This is the C-level interrupt handler
void InterruptHandler(struct Registers* regs) {
    // Handle timer interrupt (IRQ0, remapped to 32)
    if (regs->interrupt_number == 32) {
        tick_count++;
        
        // Task switch every 100 ticks
        if (tick_count % 100 == 0) {
            Schedule();
        }
        
        char tick_str[20];
        itoa(tick_count, tick_str);
        PrintKernel("Ticks: ", 8, 0);
        PrintKernel(tick_str, 8, 7);
    }

    // Send EOI to PICs
    if (regs->interrupt_number >= 0x28) { // If interrupt is from slave PIC
        outb(0xA0, 0x20); // Send EOI to slave PIC
    }
    outb(0x20, 0x20); // Send EOI to master PIC
}