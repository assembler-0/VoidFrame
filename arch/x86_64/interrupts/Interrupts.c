#include "Interrupts.h"
#include "Kernel.h"
#include "Atomics.h"
#include "Console.h"
#include "Ide.h"
#include "MLFQ.h"
#include "PS2.h"
#include "Panic.h"
#include "Pic.h"
#include "StackTrace.h"

volatile uint32_t PITTicks = 0;

// The C-level interrupt handler, called from the assembly stub
asmlinkage void InterruptHandler(Registers* regs) {
    ASSERT(regs != NULL);

    // Handle hardware interrupts first
    switch (regs->interrupt_number) {
        case 32: // Timer interrupt (IRQ 0)
            MLFQSchedule(regs);
            AtomicInc(&PITTicks);
            PICSendEOI(regs->interrupt_number);
            return;

        case 33: // Keyboard interrupt (IRQ 1)
            KeyboardHandler();
            PICSendEOI(regs->interrupt_number);
            return;

        case 34:
            PICSendEOI(regs->interrupt_number);
            return;

        case 44: // mouse (IRQ 12)
            MouseHandler();
            PICSendEOI(regs->interrupt_number);
            return;

        case 46: // IDE Primary (IRQ 14)
            IDEPrimaryIRQH();
            PICSendEOI(regs->interrupt_number);
            return;

        case 47: // IDE Secondary (IRQ 15)
            IDESecondaryIRQH();
            PICSendEOI(regs->interrupt_number);
            return;

        // Handle other hardware interrupts (34-45)
        case 35 ... 43: case 45: // passthrough
            PrintKernelWarning("[IRQ] Unhandled hardware interrupt: ");
            PrintKernelInt(regs->interrupt_number - 32);
            PrintKernelWarning("\n");
            PICSendEOI(regs->interrupt_number);
            return;
    }

    // Handle CPU exceptions (0-31)
    static char panic_message[256];

    switch (regs->interrupt_number) {
        case 6:  // Invalid Opcode
        case 8:  // Double Fault
        case 13: // General Protection Fault
        case 14: // Page Fault
        {
            // Perform deep fault analysis
            FaultContext ctx = {0};
            AnalyzeFault(regs, &ctx);

            // Print detailed information
            PrintDetailedFaultInfo(&ctx, regs);
            delay(100000000);
            // Still panic, but now with much more info
            PanicFromInterrupt(ctx.fault_reason, regs);
            break;
        }

        default: // All other exceptions
        {
            char int_str[20], rip_str[20];
            itoa(regs->interrupt_number, int_str);
            htoa(regs->rip, rip_str);

            strcpy(panic_message, "Unhandled Exception #");
            strcat(panic_message, int_str);
            strcat(panic_message, " at ");
            strcat(panic_message, rip_str);
            PanicFromInterrupt(panic_message, regs);
            break;
        }
    }
}