#ifndef VOIDFRAME_STACKTRACE_H
#define VOIDFRAME_STACKTRACE_H

#include <stdint.h>
#include <x64.h>

#define MAX_STACK_FRAMES 16

typedef struct {
    uint64_t rip;
    uint64_t rbp;
    const char* symbol_name; // TODO: Symbol lookup
} StackFrame;

typedef struct {
    StackFrame frames[MAX_STACK_FRAMES];
    int frame_count;
    uint64_t fault_address;  // CR2 for page faults
    uint32_t error_code;
    const char* fault_reason;
} FaultContext;

// Core stack tracing functions
int WalkStack(uint64_t rbp, uint64_t rip, StackFrame* frames, int max_frames);
void AnalyzeFault(Registers* regs, FaultContext* ctx);
void PrintDetailedFaultInfo(FaultContext* ctx, Registers* regs);

// Fault-specific analyzers
void AnalyzePageFault(uint64_t cr2, uint64_t error_code, FaultContext* ctx);
void AnalyzeGPF(uint64_t error_code, FaultContext* ctx);
void AnalyzeInvalidOpcode(uint64_t rip, FaultContext* ctx);


#endif // VOIDFRAME_STACKTRACE_H
