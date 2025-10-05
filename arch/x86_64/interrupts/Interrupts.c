#include "Interrupts.h"
#include "APIC.h"
#include "Atomics.h"
#include "Console.h"
#include "Ide.h"
#include "Kernel.h"
#include "PS2.h"
#include "PageFaultHandler.h"
#include "Panic.h"
#include "Scheduler.h"
#include "StackTrace.h"
#include "ethernet/Network.h"

volatile uint32_t APICticks = 0;

uint64_t ToIRQ(const uint64_t irn) {
    return irn >= 32 && irn <= 255 ? irn - 32 : -1;
}

// The C-level interrupt handler, called from the assembly stub
asmlinkage void InterruptHandler(Registers* regs) {
    ASSERT(regs != NULL);

    // Handle hardware interrupts first
    if (regs->interrupt_number >= 32) switch (regs->interrupt_number) {
        case 32: // Timer interrupt (IRQ 0)
            Schedule(regs);
            AtomicInc(&APICticks);
            Net_Poll();
            ApicSendEoi();
            return;

        case 33: // Keyboard interrupt (IRQ 1)
            PS2Handler();
            ApicSendEoi();
            return;

        case 34:
            ApicSendEoi();
            return;

        case 44: // mouse (IRQ 12)
            PS2Handler();
            ApicSendEoi();
            return;

        case 45: // FPU error IRQ 13
            ApicSendEoi();
            return;

        case 46: // IDE Primary (IRQ 14)
            IDEPrimaryIRQH();
            ApicSendEoi();
            return;

        case 47: // IDE Secondary (IRQ 15)
            IDESecondaryIRQH();
            ApicSendEoi();
            return;

        // Handle other hardware interrupts (34-45)
        case 35 ... 43: // passthrough
            PrintKernelWarning("[IRQ] Unhandled hardware interrupt: ");
            PrintKernelInt(regs->interrupt_number - 32);
            PrintKernelWarning("\n");
            ApicSendEoi();
            return;
        default: break; // fallback to CPU exceptions (should not be possible as already clamped)
    }

    else switch (regs->interrupt_number) {
        case 0:  // Divide by Zero
        case 1:  // Debug
        case 2:  // NMI
        case 3:  // Breakpoint
        case 4:  // Overflow
        case 5:  // Bound Range Exceeded
        case 6:  // Invalid Opcode
        case 7:  // Device Not Available
        case 8:  // Double Fault
        case 9:  // Coprocessor Segment Overrun
        case 10: // Invalid TSS
        case 11: // Segment Not Present
        case 12: // Stack Fault
        case 13: // General Protection Fault
        case 14: // Page Fault
        case 15: // Reserved
        case 16: // x87 FPU Floating-Point exception
        case 17: // Alignment Check
        case 18: // Machine Check
        case 19: // SIMD Floating-Point exception
        case 20: // Virtualization exception
        case 21: // Control protocol exception
        case 22 ... 27: // Reserved
        case 28: // Hypervisor injection exception
        case 29: // VMM communication exception
        case 30: // Security exception
        case 31: // Reserved
        {
            if (regs->interrupt_number == 14) {
                FaultResult result = HandlePageFault(regs);
                switch (result) {
                    case FAULT_HANDLED:
                        // Fault was handled, continue execution
                        return;

                    case FAULT_KILL_PROCESS:
                        // Kill the offending process
                        PrintKernelWarning("Killing process ");
                        PrintKernelInt(GetCurrentProcess()->pid);
                        PrintKernelWarning(" due to page fault\n");
                        KillCurrentProcess("Page Fault (segmentation fault)");
                        // Switch to the next ctx immediately
                        Schedule(regs);
                        return;

                    case FAULT_RETRY:
                        // Retry the instruction
                        return;

                    case FAULT_PANIC_KERNEL:
                    default:
                        // Fall through to panic handling
                        break;
                }
            }
            FaultContext ctx = {0};
            AnalyzeFault(regs, &ctx);
            PrintDetailedFaultInfo(&ctx, regs);
            RegistersDumpT dump = {0};
            DumpRegisters(&dump);
            // Override with fault context where applicable
            dump.rip    = regs->rip;
            dump.cs     = regs->cs;
            dump.rflags = regs->rflags;
            dump.rsp    = regs->rsp;
            dump.ss     = regs->ss;
            PrintRegisters(&dump);
            PanicFromInterrupt(ctx.fault_reason, regs);
        }

        default:
        {
            // All handled
        }
    }
}