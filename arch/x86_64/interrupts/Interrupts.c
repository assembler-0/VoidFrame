#include "Interrupts.h"

#include "APIC.h"
#include "Atomics.h"
#include "Console.h"
#include "Ide.h"
#include "Kernel.h"
#include "MLFQ.h"
#include "PS2.h"
#include "PageFaultHandler.h"
#include "Panic.h"
#include "StackTrace.h"
#include "ethernet/Network.h"

volatile uint32_t APICticks = 0;

// The C-level interrupt handler, called from the assembly stub
asmlinkage void InterruptHandler(Registers* regs) {
    ASSERT(regs != NULL);

    // Handle hardware interrupts first
    if (regs->interrupt_number >= 32) switch (regs->interrupt_number) {
        case 32: // Timer interrupt (IRQ 0)
            MLFQSchedule(regs);
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

        case 46: // IDE Primary (IRQ 14)
            IDEPrimaryIRQH();
            ApicSendEoi();
            return;

        case 47: // IDE Secondary (IRQ 15)
            IDESecondaryIRQH();
            ApicSendEoi();
            return;

        // Handle other hardware interrupts (34-45)
        case 35 ... 43: case 45: // passthrough
            PrintKernelWarning("[IRQ] Unhandled hardware interrupt: ");
            PrintKernelInt(regs->interrupt_number - 32);
            PrintKernelWarning("\n");
            ApicSendEoi();
            return;
        default: break; // fallback to CPU exceptions (should not be possible as already clamped)
    }

    else switch (regs->interrupt_number) {
        case 14: // Page Fault - Handle gracefully
        {
            FaultResult result = HandlePageFault(regs);
            
            switch (result) {
                case FAULT_HANDLED:
                    // Fault was handled, continue execution
                    return;
                    
                case FAULT_KILL_PROCESS:
                    // Kill the offending process
                    PrintKernelWarning("Killing process ");
                    PrintKernelInt(MLFQGetCurrentProcess()->pid);
                    PrintKernelWarning(" due to page fault\n");
                    MLFQKillCurrentProcess("Page Fault (segmentation fault)");
                    // Switch to the next ctx immediately
                    MLFQSchedule(regs);
                    return;
                    
                case FAULT_RETRY:
                    // Retry the instruction
                    return;
                    
                case FAULT_PANIC_KERNEL:
                default:
                    // Fall through to panic handling
                    break;
            }
            
            // If we get here, it's a serious kernel fault
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
            PanicFromInterrupt("Unrecoverable page fault", regs);
        }
        
        case 6:  // Invalid Opcode
        case 8:  // Double Fault  
        case 13: // General Protection Fault
        {
            // These are still serious - analyze and panic
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

        default: // All other exceptions
        {
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
            PanicFromInterrupt("Unrecoverable page fault", regs);
        }
    }
}