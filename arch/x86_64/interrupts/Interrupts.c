#include "stdint.h"
#include "Kernel.h"
#include "Io.h"
#include "Panic.h"
#include "Process.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static uint64_t tick_count = 0;

char * itoa(uint64_t num) {
    char* str;
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
    return str;
}

// Fast tick display using direct memory write
static void FastDisplayTicks(uint64_t ticks) {
    uint16_t *vidptr = (uint16_t*)0xb8000;
    int pos = 20 * 80; // Line 20

    // Write "Ticks: "
    vidptr[pos++] = (0x03 << 8) | 'T';
    vidptr[pos++] = (0x03 << 8) | 'i';
    vidptr[pos++] = (0x03 << 8) | 'c';
    vidptr[pos++] = (0x03 << 8) | 'k';
    vidptr[pos++] = (0x03 << 8) | 's';
    vidptr[pos++] = (0x03 << 8) | ':';
    vidptr[pos++] = (0x03 << 8) | ' ';

    // Fast number display
    if (ticks == 0) {
        vidptr[pos] = (0x03 << 8) | '0';
        return;
    }

    char buf[20];
    int i = 0;
    uint64_t temp = ticks;

    while (temp > 0) {
        buf[i++] = '0' + (temp % 10);
        temp /= 10;
    }

    while (i > 0) {
        vidptr[pos++] = (0x03 << 8) | buf[--i];
    }
}


static void FatalExceptionHandler(const char* message, uint64_t interrupt_number) {
    PrintKernelWarning(message);
    PrintKernelWarning(" at interrupt: ");
    PrintKernelInt(interrupt_number);
    PrintKernelWarning("\n");

    Process* current_process = GetCurrentProcess();
    if (current_process != NULL) {
        KillProcess(current_process->pid);
        RequestSchedule();
    } else {
        Panic("Cannot terminate process, NULL process found!");
    }
}


// The C-level interrupt handler
void InterruptHandler(struct Registers* regs) {
    ASSERT(regs != NULL);
    if (regs->interrupt_number == 32) {
        tick_count++;
        // FastDisplayTicks(tick_count);
        FastSchedule(regs);
        outb(0x20, 0x20);
        return;
    }
    if (regs->interrupt_number == 13) {
        FatalExceptionHandler("FATAL EXCEPTION - Page fault (GPF handler)", regs->interrupt_number);
        return;
    }
    if (regs->interrupt_number >= 255) {
        FatalExceptionHandler("FATAL  EXCEPTION - OVERFLOWING - Cannot handle interrupt. (>256)", regs->interrupt_number);
        return;
    }

    if (regs->interrupt_number >= 40) {
        outb(0xA0, 0x20); // EOI to slave PIC
    }
    outb(0x20, 0x20); // EOI to master PIC
}