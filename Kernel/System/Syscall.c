#include "Syscall.h"
#include "Kernel.h"
#include "Process.h"
#include "Idt.h"
#include "Panic.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
extern void SyscallEntry(void);
uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    Process* current = GetCurrentProcess();
    if (unlikely(!current)) {
        Panic("Syscall from invalid process");
    }
    switch (syscall_num) {
        case SYS_EXIT:
            // Terminate current process
            if (current) {
                current->state = PROC_TERMINATED;
            }
            RequestSchedule(); // Switch to next process
            return 0;
            
        case SYS_WRITE:
            // arg1 = fd (ignored for now), arg2 = buffer, arg3 = count
            if (likely(arg1 == 1)) { // stdout
                if (unlikely(!arg2)) {
                    return -1; // NULL buffer
                }
                if (unlikely(arg3 > 4096)) {
                    return -1; // Buffer too large
                }
                
                char* buffer = (char*)arg2;
                for (uint64_t i = 0; i < arg3; i++) {
                    if (buffer[i] == '\0') break;
                    PrintKernelAt(&buffer[i], CurrentLine, CurrentColumn++);
                    if (CurrentColumn >= 80) {
                        CurrentLine++;
                        CurrentColumn = 0;
                        if (CurrentLine >= 25) CurrentLine = 24; // Screen bounds
                    }
                }
                return arg3;
            }
            return -1;
            
        case SYS_READ:
            // Not implemented yet
            return 0;
            
        case SYS_GETPID:
            return current ? current->pid : -1;
            
        default:
            return -1;
    }
}

void SyscallInit(void) {
    // Install syscall interrupt (0x80)
    IdtSetGate(0x80, (uint64_t)SyscallEntry, SYSCALL_SEGMENT_SELECTOR, IDT_INTERRUPT_GATE_KERNEL);
}