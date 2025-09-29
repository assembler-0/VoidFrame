/**
 * @file PageFaultHandler.h
 * @brief Graceful Page Fault Handler - Linux-style fault recovery
 */

#ifndef PAGE_FAULT_HANDLER_H
#define PAGE_FAULT_HANDLER_H

#include "stdint.h"
#include "x64.h"

// Page fault error code bits
#define PF_PRESENT    0x01  // Page was present
#define PF_WRITE      0x02  // Write access
#define PF_USER       0x04  // User mode access
#define PF_RESERVED   0x08  // Reserved bit violation
#define PF_INSTR      0x10  // Instruction fetch

// Fault handling results
typedef enum {
    FAULT_HANDLED = 0,      // Fault was handled gracefully
    FAULT_KILL_PROCESS,     // Kill the offending process
    FAULT_PANIC_KERNEL,     // Kernel fault - must panic
    FAULT_RETRY,            // Retry the instruction
} FaultResult;

// Fault context for analysis
typedef struct {
    uint64_t fault_addr;    // CR2 - faulting address
    uint64_t error_code;    // Page fault error code
    uint64_t rip;           // Instruction pointer
    uint32_t pid;           // Process ID (if applicable)
    const char* reason;     // Human-readable reason
} PageFaultInfo;

// Core page fault handler
FaultResult HandlePageFault(Registers* regs);

// Fault analysis functions
int IsKernelFault(uint64_t fault_addr, uint64_t error_code);
int IsValidUserAccess(uint64_t fault_addr, uint32_t pid);
int IsStackOverflow(uint64_t fault_addr, uint32_t pid);
int IsNullPointerDereference(uint64_t fault_addr);

// Recovery functions
FaultResult HandleKernelFault(PageFaultInfo* info);
FaultResult HandleUserFault(PageFaultInfo* info);
FaultResult HandleStackOverflow(PageFaultInfo* info);

// Logging and debugging
void LogPageFault(PageFaultInfo* info, FaultResult result);
void PrintPageFaultStats(void);

#endif // PAGE_FAULT_HANDLER_H