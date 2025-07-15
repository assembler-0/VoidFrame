#ifndef SYSCALL_H
#define SYSCALL_H

#include "stdint.h"

// System call numbers
#define SYS_EXIT    1
#define SYS_WRITE   2
#define SYS_READ    3
#define SYS_GETPID  4
#define SYS_IPC_SEND 5
#define SYS_IPC_RECV 6
#define SYSCALL_INTERRUPT_VECTOR 0x80
#define IDT_INTERRUPT_GATE_KERNEL 0x8E
#define IDT_TRAP_GATE_USER        0xEE // Present, DPL 3, 64-bit Trap Gate
#define SYSCALL_SEGMENT_SELECTOR  0x08
#define MAX_SYSCALL_BUFFER_SIZE 4096
// System call handler
void SyscallInit(void);
uint64_t Syscall(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#endif