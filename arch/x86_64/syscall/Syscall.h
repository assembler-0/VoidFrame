#ifndef VOIDFRAME_SYSCALL_H
#define VOIDFRAME_SYSCALL_H

#include <stdint.h>

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_CREATE_FILE 4
#define SYS_CREATE_DIR 5
#define SYS_DELETE 6
#define SYS_LIST_DIR 7
#define SYS_CREATE_PROCESS 8
#define SYS_KILL_PROCESS 9
#define SYS_GET_PID 10
#define SYS_YIELD 11
#define SYS_IPC_SEND_MESSAGE 12
#define SYS_IPC_RECEIVE_MESSAGE 13
#define SYS_EXIT 60

#define SYSCALL_INTERRUPT_VECTOR 80
#define IDT_INTERRUPT_GATE_KERNEL 0x8E
#define SYSCALL_SEGMENT_SELECTOR 0x08
#define MAX_SYSCALL_BUFFER_SIZE 4096

uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3);

extern void SyscallEntry();

#endif // VOIDFRAME_SYSCALL_H
