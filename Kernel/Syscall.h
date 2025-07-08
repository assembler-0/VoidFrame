#ifndef SYSCALL_H
#define SYSCALL_H

#include "stdint.h"

// System call numbers
#define SYS_EXIT    1
#define SYS_WRITE   2
#define SYS_READ    3
#define SYS_GETPID  4
#define SYSCALL_INTERRUPT_VECTOR 0x08
#define IDT_INTERRUPT_GATE_KERNEL 0x8E
// System call handler
void SyscallInit(void);
uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

#endif