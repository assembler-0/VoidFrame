#include "stdint.h"
#include "Kernel.h"
#include "Io.h"
#include "Panic.h"
#include "Process.h"
#include "VMem.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static uint64_t tick_count = 0;

// Fast tick display using direct memory write
static void FastDisplayTicks(uint64_t ticks) {
    uint16_t *vidptr = (uint16_t*)(0xb8000 + KERNEL_VIRTUAL_OFFSET);
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
    PANIC(message);
}


// The C-level interrupt handler
void InterruptHandler(struct Registers* regs) {
    ASSERT(regs != NULL);
    if (regs->interrupt_number == 32 || regs->interrupt_number == 33) { // Force ignore keyboard interrupt
        tick_count++;
        // FastDisplayTicks(tick_count);
        FastSchedule(regs);
        outb(0x20, 0x20); // EOI to master PIC
        return;
    }

    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));

    PrintKernelError("PAGE FAULT\n");
    PrintKernelError("  Address: ");
    PrintKernelHex(cr2);
    PrintKernelError("\n  Error Code: ");
    PrintKernelHex(regs->error_code);
    PrintKernelError("\n");


    if (!(regs->error_code & 0x1)) {
        PrintKernelError("  Reason: Page Not Present\n");
    } else {
        PrintKernelError("  Reason: Protection Violation\n");
    }

    if (regs->error_code & 0x2) {
        PrintKernelError("  Operation: Write\n");
    } else {
        PrintKernelError("  Operation: Read\n");
    }

    if (regs->error_code & 0x4) {
        PrintKernelError("  Mode: User\n");
    }
    else {
        PrintKernelError("  Mode: Supervisor\n");
    }

    if (regs->error_code & 0x8) {
        PrintKernelError("  Cause: Reserved bit set\n");
    }

    if (regs->error_code & 0x10) {
        PrintKernelError("  Cause: Instruction fetch\n");
    }

    PANIC_CODE("Page Fault", regs->error_code);
}

// The C-level interrupt handler
