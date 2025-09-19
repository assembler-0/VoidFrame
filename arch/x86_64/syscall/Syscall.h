#ifndef VOIDFRAME_SYSCALL_H
#define VOIDFRAME_SYSCALL_H

#include "stdint.h"

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_EXIT    60
#define SYSCALL_INTERRUPT_VECTOR 0x80
#define IDT_INTERRUPT_GATE_KERNEL 0x8E
#define SYSCALL_SEGMENT_SELECTOR  0x08
#define MAX_SYSCALL_BUFFER_SIZE 4096

uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

extern void SyscallEntry();

#endif // VOIDFRAME_SYSCALL_H
