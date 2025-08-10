#include "Interrupts.h"
#include "Console.h"
#include "Ide.h"
#include "Io.h"
#include "PS2.h"
#include "Panic.h"
#include "Process.h"
#include "MemOps.h"

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

    // Buffer for creating descriptive panic messages.
    // Made static to reside in .bss rather than on a potentially corrupt stack.
    static char panic_message[256];

    switch (regs->interrupt_number) {
        case 6: // Invalid Opcode
        {
            char rip_str[20];
            htoa(regs->rip, rip_str);
            strcpy(panic_message, "Invalid Opcode at ");
            strcat(panic_message, rip_str);
            PanicFromInterrupt(panic_message, regs);
            break;
        }

        case 13: // General Protection Fault
        {
            char ec_str[20];
            htoa(regs->error_code, ec_str);
            strcpy(panic_message, "General Protection Fault. Selector: ");
            strcat(panic_message, ec_str);
            PanicFromInterrupt(panic_message, regs);
            break;
        }

        case 14: // Page Fault
        {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            char cr2_str[20], rip_str[20];
            htoa(cr2, cr2_str);
            htoa(regs->rip, rip_str);

            strcpy(panic_message, "Page Fault accessing ");
            strcat(panic_message, cr2_str);
            strcat(panic_message, " from instruction at ");
            strcat(panic_message, rip_str);
            PanicFromInterrupt(panic_message, regs);
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