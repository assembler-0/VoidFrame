#include "StackTrace.h"
#include "Console.h"
#include "StringOps.h"
#include "VMem.h"

// Check if an address is valid for kernel access
static bool IsValidKernelAddress(uint64_t addr) {
    // Basic sanity checks - adjust these based on your memory layout
    if (addr < KERNEL_SPACE_START) return false; // Below kernel space
    if (addr >= KERNEL_SPACE_END) return false; // Too high
    
    return true;
}

int WalkStack(uint64_t rbp, uint64_t rip, StackFrame* frames, int max_frames) {
    int frame_count = 0;
    
    // Add the current frame (fault location)
    if (frame_count < max_frames) {
        frames[frame_count].rip = rip;
        frames[frame_count].rbp = rbp;
        frames[frame_count].symbol_name = "FAULT_LOCATION";
        frame_count++;
    }
    
    // Walk the stack using frame pointers
    uint64_t current_rbp = rbp;
    
    while (frame_count < max_frames && current_rbp != 0) {
        // Validate frame pointer
        if (!IsValidKernelAddress(current_rbp) || 
            !IsValidKernelAddress(current_rbp + 8)) {
            break;
        }
        
        // Read return address and previous frame pointer
        uint64_t return_addr = *(uint64_t*)(current_rbp + 8);
        uint64_t prev_rbp = *(uint64_t*)current_rbp;
        
        // Validate return address
        if (!IsValidKernelAddress(return_addr)) {
            break;
        }
        
        // Add frame to trace
        frames[frame_count].rip = return_addr;
        frames[frame_count].rbp = current_rbp;
        frames[frame_count].symbol_name = "UNKNOWN"; // TODO: Symbol lookup
        frame_count++;
        
        // Move to previous frame
        current_rbp = prev_rbp;
        
        // Prevent infinite loops
        if (prev_rbp <= current_rbp) {
            break;
        }
    }
    
    return frame_count;
}

void AnalyzePageFault(uint64_t cr2, uint64_t error_code, FaultContext* ctx) {
    ctx->fault_address = cr2;

    // Decode page fault error code bits
    bool present = (error_code & 0x1) != 0;
    bool write = (error_code & 0x2) != 0;
    bool user = (error_code & 0x4) != 0;
    bool reserved = (error_code & 0x8) != 0;
    bool instruction = (error_code & 0x10) != 0;
    
    static char reason[256];
    strcpy(reason, "Page Fault: ");
    
    if (!present) {
        strcat(reason, "Page not present, ");
    }
    if (write) {
        strcat(reason, "Write access, ");
    } else {
        strcat(reason, "Read access, ");
    }
    if (user) {
        strcat(reason, "User mode, ");
    } else {
        strcat(reason, "Kernel mode, ");
    }
    if (reserved) {
        strcat(reason, "Reserved bit violation, ");
    }
    if (instruction) {
        strcat(reason, "Instruction fetch, ");
    }
    
    // Add address classification
    if (cr2 == 0) {
        strcat(reason, "NULL pointer dereference");
    } else if (cr2 < 0x1000) {
        strcat(reason, "Low memory access (likely NULL+offset)");
    } else if (cr2 >= 0xFFFF800000000000ULL) {
        strcat(reason, "Kernel space access");
    } else {
        strcat(reason, "User space access");
    }
    
    ctx->fault_reason = reason;
}

void AnalyzeGPF(uint64_t error_code, FaultContext* ctx) {
    static char reason[256];
    
    if (error_code == 0) {
        strcpy(reason, "General Protection Fault: Invalid instruction or privilege violation");
    } else {
        strcpy(reason, "General Protection Fault: Selector error 0x");
        char ec_str[20];
        htoa(error_code, ec_str);
        strcat(reason, ec_str);
        
        // Decode selector error
        bool external = (error_code & 0x1) != 0;
        uint16_t table = (error_code >> 1) & 0x3;
        uint16_t index = (error_code >> 3) & 0x1FFF;
        
        strcat(reason, " (");
        if (external) {
            strcat(reason, "External event, ");
        }
        
        switch (table) {
            case 0: strcat(reason, "GDT"); break;
            case 1: strcat(reason, "IDT"); break;
            case 2: strcat(reason, "LDT"); break;
            case 3: strcat(reason, "IDT"); break;
        }
        
        char idx_str[20];
        itoa(index, idx_str);
        strcat(reason, " index ");
        strcat(reason, idx_str);
        strcat(reason, ")");
    }
    
    ctx->fault_reason = reason;
}

void AnalyzeInvalidOpcode(uint64_t rip, FaultContext* ctx) {
    static char reason[256];
    char rip_str[20];
    htoa(rip, rip_str);

    strcpy(reason, "Invalid Opcode at 0x");
    strcat(reason, rip_str);
    strcat(reason, " bytes: ");

    uint8_t* instr = (uint8_t*)rip;
    for (int i = 0; i < 8; i++) {
        char byte_str[4];
        htoa(instr[i], byte_str);
        strcat(reason, byte_str);
        strcat(reason, " ");
    }

    ctx->fault_reason = reason;
}


void AnalyzeFault(Registers* regs, FaultContext* ctx) {
    ctx->error_code = regs->error_code;
    
    // Walk the stack to get backtrace
    ctx->frame_count = WalkStack(regs->rbp, regs->rip, ctx->frames, MAX_STACK_FRAMES);
    
    // Analyze specific fault types
    switch (regs->interrupt_number) {
        case 0:  // Divide by Zero
            ctx->fault_reason = "Divide by Zero";
            break;
        case 1:  // Debug
            ctx->fault_reason = "Debug";
            break;
        case 2:  // NMI
            ctx->fault_reason = "NMI";
            break;
        case 3:  // Breakpoint
            ctx->fault_reason = "Breakpoint";
            break;
        case 4:  // Overflow
            ctx->fault_reason = "Overflow";
            break;
        case 5:  // Bound Range Exceeded
            ctx->fault_reason = "Bound Range Exceeded";
            break;
        case 6: // Invalid Opcode
            AnalyzeInvalidOpcode(regs->rip, ctx);
            break;
        case 7:  // Device Not Available
            ctx->fault_reason = "Device Not Available";
            break;
        case 8:  // Double Fault
            ctx->fault_reason = "Double fault";
            break;
        case 9:  // Coprocessor Segment Overrun
            ctx->fault_reason = "Coprocessor Segment Overrun";
            break;
        case 10: // Invalid TSS
            ctx->fault_reason = "Invalid TSS";
            break;
        case 11: // Segment Not Present
            ctx->fault_reason = "Segment Not Present";
            break;
        case 12: // Stack Fault
            ctx->fault_reason = "Stack Fault";
            break;
        case 13: // General Protection Fault
            AnalyzeGPF(regs->error_code, ctx);
            break;
        case 14: // Page Fault
            __asm__ volatile("mov %%cr2, %0" : "=r"(ctx->fault_address));
            AnalyzePageFault(ctx->fault_address, regs->error_code, ctx);
            break;
        case 15: // Reserved
            ctx->fault_reason = "Reserved 15";
            break;
        case 16: // x87 FPU Floating-Point exception
            ctx->fault_reason = "x87 FPU Floating-Point exception";
            break;
        case 17: // Alignment Check
            ctx->fault_reason = "Alignment Check";
            break;
        case 18: // Machine Check
            ctx->fault_reason = "Machine Check";
            break;
        case 19: // SIMD Floating-Point exception
            ctx->fault_reason = "SIMD Floating-Point exception";
            break;
        case 20: // Virtualization exception
            ctx->fault_reason = "Virtualization exception";
            break;
        case 21: // Control protocol exception
            ctx->fault_reason = "Control protocol exception";
            break;
        case 22 ... 27: // Reserved
            ctx->fault_reason = "Reserved 22-27";
            break;
        case 28: // Hypervisor injection exception
            ctx->fault_reason = "Hypervisor injection exception";
            break;
        case 29: // VMM communication exception
            ctx->fault_reason = "VMM communication exception";
            break;
        case 30: // Security exception
            ctx->fault_reason = "Security exception";
            break;
        case 31: // Reserved
            ctx->fault_reason = "Reserved 31";
            break;

        default:
            ctx->fault_reason = "Unknown fault type or reserved";
            break;
    }
}

void PrintDetailedFaultInfo(FaultContext* ctx, Registers* regs) {
    PrintKernelError("=== VOIDFRAME STACK TRACE ===\n");
    PrintKernelError("Fault Type: ");
    PrintKernelError(ctx->fault_reason);
    PrintKernelError("\n");
    
    // Print register state
    PrintKernelError("Register State:\n");
    PrintKernelError("  RAX: 0x"); PrintKernelHex(regs->rax); PrintKernelError("\n");
    PrintKernelError("  RBX: 0x"); PrintKernelHex(regs->rbx); PrintKernelError("\n");
    PrintKernelError("  RCX: 0x"); PrintKernelHex(regs->rcx); PrintKernelError("\n");
    PrintKernelError("  RDX: 0x"); PrintKernelHex(regs->rdx); PrintKernelError("\n");
    PrintKernelError("  RSI: 0x"); PrintKernelHex(regs->rsi); PrintKernelError("\n");
    PrintKernelError("  RDI: 0x"); PrintKernelHex(regs->rdi); PrintKernelError("\n");
    PrintKernelError("  RBP: 0x"); PrintKernelHex(regs->rbp); PrintKernelError("\n");
    PrintKernelError("  RSP: 0x"); PrintKernelHex(regs->rsp); PrintKernelError("\n");
    PrintKernelError("  RIP: 0x"); PrintKernelHex(regs->rip); PrintKernelError("\n");
    
    // Print stack trace
    PrintKernelError("Stack Trace:\n");
    for (int i = 0; i < ctx->frame_count; i++) {
        PrintKernelError("  Frame ");
        PrintKernelInt(i);
        PrintKernelError(": RIP=0x");
        PrintKernelHex(ctx->frames[i].rip);
        PrintKernelError(" RBP=0x");
        PrintKernelHex(ctx->frames[i].rbp);
        PrintKernelError(" (");
        PrintKernelError(ctx->frames[i].symbol_name);
        PrintKernelError(")\n");
    }
    
    // Page fault specific info
    if (regs->interrupt_number == 14) {
        PrintKernelError("Fault Address: 0x");
        PrintKernelHex(ctx->fault_address);
        PrintKernelError("\n");
    }
}