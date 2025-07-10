#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"
#include "../Core/Ipc.h"

#define MAX_PROCESSES 64
#define STACK_SIZE 4096

#define PROC_PRIV_SYSTEM    0   // Highest privilege (kernel services)
#define PROC_PRIV_USER      1   // User processes
#define PROC_PRIV_RESTRICTED 2  // Restricted processes

typedef struct {
    uint64_t magic;          // Magic number for validation
    uint32_t creator_pid;    // PID of creating process
    uint8_t  privilege;      // Process privilege level
    uint8_t  flags;          // Security flags
    uint16_t checksum;       // Simple checksum
} __attribute__((packed)) SecurityToken;

typedef enum {
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_TERMINATED = 0
} ProcessState;

// DO NOT TOUCH THIS STRUCTURE - must match interrupt ASM stack layout
typedef struct Registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rsi, rdi, rdx, rcx, rbx, rax;
    uint64_t ds, es, fs, gs;
    uint64_t interrupt_number;
    uint64_t error_code;
    uint64_t rip, cs, rflags;
    uint64_t rsp, ss;
} __attribute__((packed)) Registers;

// Use the same structure for context switching to avoid mismatches
typedef struct Registers ProcessContext;

typedef struct {
    uint32_t pid;
    ProcessState state;
    void* stack;
    uint8_t priority;
    uint8_t is_user_mode;
    uint8_t privilege_level;
    uint8_t _padding;
    SecurityToken token;
    MessageQueue ipc_queue;
    ProcessContext context;
} Process;

void ProcessInit(void);
uint32_t CreateProcess(void (*entry_point)(void));
uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege);
void RequestSchedule();
int ShouldSchedule();
void Schedule(void);
void Yield(void);
Process* GetCurrentProcess(void);
Process* GetProcessByPid(uint32_t pid);
void ScheduleFromInterrupt(struct Registers* regs);
void RegisterSecurityManager(uint32_t pid);
void SecureKernelIntegritySubsystem(void);
void SystemService(void);
#endif