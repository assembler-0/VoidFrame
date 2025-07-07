#include "stdint.h"
#include "Kernel.h"
#include "Io.h"
#include "Process.h"

struct Registers {
    uint64_t rax, rbx, rcx, rdx, rdi, rsi, rbp, r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

static uint64_t tick_count = 0;

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

// The C-level interrupt handler
void InterruptHandler(const struct Registers* regs) {
    // Handle hardware interrupts from the PIC (Programmable Interrupt Controller)
    if (regs->interrupt_number >= 32 && regs->interrupt_number < 48) {

        // If it's the timer interrupt (IRQ0), update ticks and schedule
        if (regs->interrupt_number == 32) {
            tick_count++;

            // Display tick count for debugging
            char tick_str[20];
            itoa(tick_count, tick_str);
            PrintKernelAt("Ticks: ", 20, 0);
            PrintKernelAt(tick_str, 20, 7);
        }

        // Send End-of-Interrupt (EOI) to the appropriate PIC(s)
        // This MUST be done before scheduling.
        if (regs->interrupt_number >= 40) {
            outb(0xA0, 0x20); // EOI to slave PIC
        }
        outb(0x20, 0x20); // EOI to master PIC

        // If it was the timer interrupt, call the scheduler to preempt the current task
        if (regs->interrupt_number == 32) {
            Schedule();
        }
    }
    // Note: This does not handle CPU exceptions (interrupt numbers 0-31)
}