#include "stdint.h"
#include "Kernel.h"

// Structure to save CPU registers
struct Registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rsi, rdi, rdx, rcx, rbx, rax;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

// This is the C-level interrupt handler
void InterruptHandler(struct Registers* regs) {
    // For now, let's just print the interrupt number to the screen
    // In a real OS, you'd have a jump table or a more sophisticated dispatcher
    char num_str[10];
    int i = 0;
    uint64_t temp = regs->interrupt_number;

    if (temp == 0) {
        num_str[i++] = '0';
    } else {
        while (temp > 0) {
            num_str[i++] = (temp % 10) + '0';
            temp /= 10;
        }
    }
    num_str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char t = num_str[start];
        num_str[start] = num_str[end];
        num_str[end] = t;
        start++;
        end--;
    }

    PrintKernel("Interrupt: ", (char*)0xb8000, 0, 0);
    PrintKernel(num_str, (char*)0xb8000, 0, 11 * 2);

    // If it's a page fault (interrupt 14), print the error code
    if (regs->interrupt_number == 14) {
        PrintKernel(" Error Code: ", (char*)0xb8000, 0, 11 * 2 + i * 2);
        i = 0;
        temp = regs->error_code;
        if (temp == 0) {
            num_str[i++] = '0';
        } else {
            while (temp > 0) {
                num_str[i++] = (temp % 10) + '0';
                temp /= 10;
            }
        }
        num_str[i] = '\0';
        start = 0;
        end = i - 1;
        while (start < end) {
            char t = num_str[start];
            num_str[start] = num_str[end];
            num_str[end] = t;
            start++;
            end--;
        }
        PrintKernel(num_str, (char*)0xb8000, 0, 11 * 2 + i * 2 + 13 * 2);
    }

    // Halt the CPU to prevent further execution after an unhandled interrupt
    asm volatile("hlt");
}
