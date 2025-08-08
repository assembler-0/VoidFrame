#include "Syscall.h"

#include "Console.h"
#include "Gdt.h"
#include "Idt.h"
#include "Ipc.h"
#include "MemOps.h"
#include "Panic.h"
#include "Process.h"
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
extern void SyscallEntry(void);
uint64_t Syscall(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    Process* current = GetCurrentProcess();
    if (unlikely(!current)) {
        Panic("Syscall from invalid process");
    }
    switch (syscall_num) {
        case SYS_EXIT:
            // Terminate current process
            if (current) {
                KillProcess(current->pid); // arg1 can be exit code
            }
            // Should not return from TerminateProcess
            while(1) { __asm__ __volatile__("hlt"); }
            return 0;
            
        case SYS_WRITE:
            // arg1 = fd (ignored for now), arg2 = buffer, arg3 = count
            if (likely(arg1 == 1)) { // stdout
                if (unlikely(!arg2)) {
                    return -1; // NULL buffer
                }
                // Limit the size to prevent buffer overflows in kernel space
                if (unlikely(arg3 > MAX_SYSCALL_BUFFER_SIZE)) {
                    return -1; // Buffer too large
                }
                
                // Copy user buffer to a kernel-controlled buffer
                char kernel_buffer[MAX_SYSCALL_BUFFER_SIZE + 1]; // +1 for null terminator
                FastMemcpy(kernel_buffer, (const void*)arg2, arg3);
                kernel_buffer[arg3] = '\0'; // Ensure null termination
                
                PrintKernel(kernel_buffer);
                return arg3;
            }
            return -1;
            
        case SYS_READ:
            // Not implemented yet
            return 0;
            
        case SYS_GETPID:
            return current ? current->pid : -1;

        case SYS_IPC_SEND:
            // arg1 = target_pid, arg2 = message
            IpcSendMessage((uint32_t)arg1, (IpcMessage*)arg2);
            return 0;

        case SYS_IPC_RECV:
            // arg1 = message_buffer
            return IpcReceiveMessage((IpcMessage*)arg1);
            
        default:
            return -1;
    }
}

void SyscallInit(void) {
    // Install syscall interrupt (0x80)
    IdtSetGate(0x80, (uint64_t)SyscallEntry, KERNEL_CODE_SELECTOR, IDT_TRAP_GATE_USER);
}