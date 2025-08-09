#include "Interrupts.h"
#include "Console.h"
#include "Ide.h"
#include "Io.h"
#include "Keyboard.h"
#include "Panic.h"
#include "Process.h"

// The C-level interrupt handler, called from the assembly stub
void InterruptHandler(Registers* regs) {
    ASSERT(regs != NULL);

    // Handle hardware interrupts first
    switch (regs->interrupt_number) {
        case 32: // Timer interrupt
            FastSchedule(regs);
            outb(0x20, 0x20); // EOI to master PIC
            return;

        case 33: // Keyboard interrupt
            KeyboardHandler();
            outb(0x20, 0x20); // EOI to master PIC
            return;

        case 46: // IDE Primary (IRQ 14)
            IDEPrimaryIRQH();
            outb(0xA0, 0x20); // EOI to slave PIC
            outb(0x20, 0x20); // EOI to master PIC
            return;

        case 47: // IDE Secondary (IRQ 15)
            IDESecondaryIRQH();
            outb(0xA0, 0x20); // EOI to slave PIC
            outb(0x20, 0x20); // EOI to master PIC
            return;

        // Handle other hardware interrupts (34-45)
        case 34 ... 45:
            PrintKernelWarning("[IRQ] Unhandled hardware interrupt: ");
            PrintKernelInt(regs->interrupt_number - 32);
            PrintKernelWarning("\n");

            // Send EOI to the appropriate PIC
            if (regs->interrupt_number >= 40) {
                outb(0xA0, 0x20); // EOI to slave PIC
            }
            outb(0x20, 0x20); // EOI to master PIC
            return;
    }

    // Handle CPU exceptions (0-31)

    switch (regs->interrupt_number) {
        case 6: // Invalid Opcode
            PrintKernelError("  TYPE: Invalid Opcode (UD)\n");
            PrintKernelError("  RIP: ");
            PrintKernelHex(regs->rip);
            PrintKernelError("\n");
            PANIC("Invalid Opcode");
            break;

        case 13: // General Protection Fault
            PrintKernelError("  TYPE: General Protection Fault (GPF)\n");
            PrintKernelError("  RIP: ");
            PrintKernelHex(regs->rip);
            PrintKernelError("\n  Error Code: ");
            PrintKernelHex(regs->error_code);
            PANIC_CODE(" General Protection Fault\n", regs->error_code);
            break;

        case 14: // Page Fault
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));

            PrintKernelError("  TYPE: Page Fault (PF)\n");
            PrintKernelError("  Faulting Address: ");
            PrintKernelHex(cr2);
            PrintKernelError("\n  Error Code: ");
            PrintKernelHex(regs->error_code);
            PrintKernelError("\n  Details:\n");

            if (!(regs->error_code & 0x1)) PrintKernelError("    - Reason: Page Not Present\n");
            else PrintKernelError("    - Reason: Protection Violation\n");

            if (regs->error_code & 0x2) PrintKernelError("    - Operation: Write\n");
            else PrintKernelError("    - Operation: Read\n");

            if (regs->error_code & 0x4) PrintKernelError("    - Mode: User\n");
            else PrintKernelError("    - Mode: Supervisor\n");

            if (regs->error_code & 0x8) PrintKernelError("    - Cause: Reserved bit set\n");
            if (regs->error_code & 0x10) PrintKernelError("    - Cause: Instruction Fetch\n");

            PANIC_CODE("Page Fault", regs->error_code);
            break;

        default: // All other exceptions
            PrintKernelError("  TYPE: Unhandled Exception\n");
            PrintKernelError("  Interrupt Number: ");
            PrintKernelInt(regs->interrupt_number);
            PrintKernelError("\n  RIP: ");
            PrintKernelHex(regs->rip);
            PrintKernelError("\n  Error Code: ");
            PrintKernelHex(regs->error_code);
            PrintKernelError("\n");
            PANIC("Unhandled CPU Exception");
            break;
    }
}