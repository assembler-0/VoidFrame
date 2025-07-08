#include "stdint.h"
#include "Kernel.h"
#include "Io.h"
#include "Process.h"

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
void InterruptHandler(struct Registers* regs) {
    // Handle timer interrupt (IRQ0, remapped to 32)
    if (regs->interrupt_number == 32) {
        tick_count++;
        char tick_str[20];
        itoa(tick_count, tick_str);
        PrintKernelAt("Ticks: ", 20, 0);
        PrintKernelAt(tick_str, 20, 7);
        
        // Send EOI to master PIC
        outb(0x20, 0x20);
        
        // Do preemptive scheduling directly
        ScheduleFromInterrupt(regs);
        return;
    }
    
    // Send EOI to PICs for other hardware interrupts
    if (regs->interrupt_number >= 40) {
        outb(0xA0, 0x20); // EOI to slave PIC
    }
    outb(0x20, 0x20); // EOI to master PIC
}