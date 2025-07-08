#include "Syscall.h"
#include "Kernel.h"
#include "Process.h"
#include "Idt.h"
extern void SyscallEntry(void);
/**
 * Handles system calls by dispatching based on the syscall number and arguments.
 *
 * Supports process termination, writing to stdout, retrieving the current process ID, and a stub for reading.
 * Returns syscall-specific results, or -1 for unsupported syscalls.
 *
 * @param syscall_num The system call number to execute.
 * @param arg1 First argument to the system call (usage depends on syscall).
 * @param arg2 Second argument to the system call (usage depends on syscall).
 * @param arg3 Third argument to the system call (usage depends on syscall).
 * @return The result of the system call, or -1 if the syscall is not supported.
 */
uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (syscall_num) {
        case SYS_EXIT:
            // Terminate current process
            GetCurrentProcess()->state = PROC_TERMINATED;
            Schedule(); // Switch to next process
            return 0;
            
        case SYS_WRITE:
            // arg1 = fd (ignored for now), arg2 = buffer, arg3 = count
            if (arg1 == 1) { // stdout
                char* buffer = (char*)arg2;
                for (uint64_t i = 0; i < arg3; i++) {
                    if (buffer[i] == '\0') break;
                    PrintKernelAt(&buffer[i], CurrentLine, CurrentColumn++);
                    if (CurrentColumn >= 80) {
                        CurrentLine++;
                        CurrentColumn = 0;
                    }
                }
                return arg3;
            }
            return -1;
            
        case SYS_READ:
            // Not implemented yet
            return 0;
            
        case SYS_GETPID:
            return GetCurrentProcess()->pid;
            
        default:
            return -1;
    }
}

/**
 * Installs the system call interrupt handler at interrupt vector 0x80.
 *
 * Sets up the IDT entry for system calls using the SyscallEntry handler.
 */
void SyscallInit(void) {
    // Install syscall interrupt (0x80)
    IdtSetGate(0x80, (uint64_t)SyscallEntry, 0x08, 0x8E);
}