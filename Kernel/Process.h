#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"

#define MAX_PROCESSES 64
#define STACK_SIZE 4096

typedef enum {
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_TERMINATED
} ProcessState;

typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
} ProcessContext;

typedef struct {
    uint32_t pid;
    ProcessState state;
    ProcessContext context;
    void* stack;
    uint64_t priority;
} Process;

void ProcessInit(void);
uint32_t CreateProcess(void (*entry_point)(void));
void Schedule(void);
int ShouldSchedule(void);
void RequestSchedule(void);
void Yield(void);
Process* GetCurrentProcess(void);

#endif