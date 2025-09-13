/**
 * @file PageFaultHandler.c
 * @brief Linux-style graceful page fault handling
 */

#include "PageFaultHandler.h"
#include "Console.h"
#include "MLFQ.h"
#include "Panic.h"
#include "PMem.h"
#include "StackTrace.h"
#include "VMem.h"

// Statistics
static uint64_t total_faults = 0;
static uint64_t handled_faults = 0;
static uint64_t kernel_faults = 0;
static uint64_t user_faults = 0;

FaultResult HandlePageFault(Registers* regs) {
    total_faults++;
    
    // Get fault address from CR2
    uint64_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    PageFaultInfo info = {
        .fault_addr = fault_addr,
        .error_code = regs->error_code,
        .rip = regs->rip,
        .pid = MLFQGetCurrentProcess()->pid,  // Get current process ID
        .reason = "Unknown"
    };
    
    FaultResult result;
    
    // Determine fault type and handle accordingly
    if (IsKernelFault(fault_addr, regs->error_code)) {
        kernel_faults++;
        result = HandleKernelFault(&info);
    } else {
        user_faults++;
        result = HandleUserFault(&info);
    }
    
    // Log the fault for debugging
    LogPageFault(&info, result);
    
    if (result == FAULT_HANDLED) {
        handled_faults++;
    }
    
    return result;
}

int IsKernelFault(uint64_t fault_addr, uint64_t error_code) {
    // Check if fault occurred in kernel mode
    if (!(error_code & PF_USER)) {
        return 1;  // Kernel mode fault
    }
    
    // Check if accessing kernel address space
    if (fault_addr >= KERNEL_SPACE_START) {
        return 1;  // User trying to access kernel space
    }
    
    return 0;  // User mode fault in user space
}

int IsNullPointerDereference(uint64_t fault_addr) {
    // NULL pointer or very low addresses (first 4KB)
    return fault_addr < 0x1000;
}

int IsStackOverflow(uint64_t fault_addr, uint32_t pid) {
    // TODO: Check if fault_addr is just below process stack
    // For now, detect if it's in typical stack overflow range
    (void)pid;  // Unused for now
    
    // Heuristic: addresses just below common stack locations
    if (fault_addr >= 0x7FFFFF000000ULL && fault_addr < 0x800000000000ULL) {
        return 1;
    }
    
    return 0;
}

FaultResult HandleKernelFault(PageFaultInfo* info) {
    // Kernel faults are serious - analyze carefully
    
    if (IsNullPointerDereference(info->fault_addr)) {
        info->reason = "Kernel NULL pointer dereference";
        
        // Log detailed info but don't panic immediately
        PrintKernelError("[KERNEL FAULT] NULL pointer at RIP: 0x");
        PrintKernelHex(info->rip);
        PrintKernelError("\n");
        
        // For now, still panic on kernel NULL pointers
        return FAULT_PANIC_KERNEL;
    }
    
    // Check if it's a legitimate kernel page fault that can be handled
    if (!(info->error_code & PF_PRESENT)) {
        // Page not present - might be swapped out or lazy allocation
        info->reason = "Kernel page not present";
    }
    
    // Check for stack overflow in kernel
    if (info->fault_addr < 0x1000000) {  // Very low address
        info->reason = "Kernel stack overflow or corruption";
        return FAULT_PANIC_KERNEL;
    }
    
    // Unknown kernel fault - this is bad
    info->reason = "Unknown kernel fault";
    return FAULT_PANIC_KERNEL;
}

FaultResult HandleUserFault(PageFaultInfo* info) {
    // User faults can often be handled gracefully
    
    if (IsNullPointerDereference(info->fault_addr)) {
        info->reason = "User NULL pointer dereference";
        PrintKernelWarning("[USER FAULT] Process ");
        PrintKernelInt(info->pid);
        PrintKernelWarning(" dereferenced NULL pointer\n");
        return FAULT_KILL_PROCESS;
    }
    
    if (IsStackOverflow(info->fault_addr, info->pid)) {
        info->reason = "User stack overflow";
        PrintKernelWarning("[USER FAULT] Process ");
        PrintKernelInt(info->pid);
        PrintKernelWarning(" stack overflow\n");
        return FAULT_KILL_PROCESS;
    }
    
    // Check for invalid memory access
    if (info->fault_addr >= KERNEL_SPACE_START) {
        info->reason = "User attempted kernel access";
        PrintKernelWarning("[SECURITY] Process ");
        PrintKernelInt(info->pid);
        PrintKernelWarning(" attempted to access kernel memory: 0x");
        PrintKernelHex(info->fault_addr);
        PrintKernelWarning("\n");
        return FAULT_KILL_PROCESS;
    }
    
    // Check if it's a valid user page that's not present
    if (!(info->error_code & PF_PRESENT)) {
        info->reason = "User page not present";
    }
    
    // Default: kill the process for unknown user faults
    info->reason = "Invalid user memory access";
    return FAULT_KILL_PROCESS;
}

static int TryHandleKernelPageNotPresent(uint64_t fault_addr) {
    // Check if this is in a valid kernel heap region
    if ((fault_addr >= VIRT_ADDR_SPACE_LOW_START && fault_addr < VIRT_ADDR_SPACE_LOW_END) ||
        (fault_addr >= VIRT_ADDR_SPACE_HIGH_START && fault_addr < VIRT_ADDR_SPACE_HIGH_END)) {
        
        // Try to allocate and map the page
        void* phys_page = AllocPage();
        if (phys_page) {
            uint64_t page_addr = fault_addr & ~0xFFF;  // Page align
            if (VMemMap(page_addr, (uint64_t)phys_page, PAGE_WRITABLE) == VMEM_SUCCESS) {
                PrintKernel("[LAZY] Allocated kernel page at 0x");
                PrintKernelHex(page_addr);
                PrintKernel("\n");
                return 1;  // Success
            }
            FreePage(phys_page);  // Failed to map, free the page
        }
    }
    
    return 0;  // Cannot handle
}

static int TryHandleUserPageNotPresent(uint64_t fault_addr, uint32_t pid) {
    (void)pid;  // TODO: Use process-specific memory management
    
    // For now, just check if it's in valid user space
    if (fault_addr < VIRT_ADDR_SPACE_LOW_END) {
        // Could implement demand paging here
        PrintKernel("[USER] Demand paging not implemented for 0x");
        PrintKernelHex(fault_addr);
        PrintKernel("\n");
    }
    
    return 0;  // Not implemented yet
}

void LogPageFault(PageFaultInfo* info, FaultResult result) {
    // Only log interesting faults to avoid spam
    if (result != FAULT_HANDLED) {
        PrintKernel("[PF] Addr: 0x");
        PrintKernelHex(info->fault_addr);
        PrintKernel(" RIP: 0x");
        PrintKernelHex(info->rip);
        PrintKernel(" PID: ");
        PrintKernelInt(info->pid);
        PrintKernel(" - ");
        PrintKernel(info->reason);
        PrintKernel("\n");
    }
}

void PrintPageFaultStats(void) {
    PrintKernel("[PF STATS] Total: ");
    PrintKernelInt(total_faults);
    PrintKernel(", Handled: ");
    PrintKernelInt(handled_faults);
    PrintKernel(", Kernel: ");
    PrintKernelInt(kernel_faults);
    PrintKernel(", User: ");
    PrintKernelInt(user_faults);
    PrintKernel("\n");
    
    if (total_faults > 0) {
        uint64_t success_rate = (handled_faults * 100) / total_faults;
        PrintKernel("[PF STATS] Success rate: ");
        PrintKernelInt(success_rate);
        PrintKernel("%\n");
    }
}