#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"
#include "../Core/Ipc.h"
#include "../Drivers/Cpu.h"

#define MAX_PROCESSES 64
#define STACK_SIZE 4096

#define MAX_PRIORITY_LEVELS 4
#define QUANTUM_BASE 10  // Base time quantum in ticks
#define BOOST_INTERVAL 100  // Boost all processes every 100 ticks

#define PROC_PRIV_SYSTEM    0   // Highest privilege (kernel services)
#define PROC_PRIV_USER      1   // User processes
#define PROC_PRIV_RESTRICTED 2  // Restricted processes

typedef struct {
    uint64_t magic;
    uint32_t creator_pid;
    uint8_t  privilege;
    uint8_t  flags;
    uint64_t creation_tick;
    uint64_t checksum;
} __attribute__((packed)) SecurityToken;

typedef enum {
    PROC_TERMINATED = 0,  // IMPORTANT: Keep this as 0 since your code expects it
    PROC_READY,
    PROC_RUNNING,
    PROC_BLOCKED,
    PROC_ZOMBIE,        // New: Waiting for cleanup
    PROC_DYING          // New: In process of termination
} ProcessState;

typedef enum {
    TERM_NORMAL = 0,    // Normal exit
    TERM_KILLED,        // Killed by another process
    TERM_CRASHED,       // Crashed/exception
    TERM_SECURITY,      // Security violation
    TERM_RESOURCE       // Resource exhaustion
} TerminationReason;
// Use the same structure for context switching to avoid mismatches
typedef struct Registers ProcessContext;

typedef struct SchedulerNode {
    uint32_t slot;
    struct SchedulerNode* next;
    struct SchedulerNode* prev;
} SchedulerNode;

typedef struct {
    uint32_t pid;
    ProcessState state;
    void* stack;
    uint8_t priority;
    uint8_t is_user_mode;
    uint8_t privilege_level;
    uint8_t _padding;
    uint32_t weight; // Base weight for scheduling (legacy - can remove later)
    uint64_t cpu_time_accumulated; // Accumulated CPU time
    int32_t dynamic_priority_score; // Score for dynamic adjustment (legacy - can remove later)
    TerminationReason term_reason;
    uint32_t exit_code;
    uint64_t termination_time;
    uint32_t parent_pid;        // For process hierarchy
    SecurityToken token;
    MessageQueue ipc_queue;
    ProcessContext context;
    SchedulerNode* scheduler_node;
    uint64_t creation_time;
} Process;

typedef struct {
    uint32_t process_slots[MAX_PROCESSES];
    SchedulerNode* head;
    SchedulerNode* tail;
    uint32_t count;
    uint32_t quantum;  // Time quantum for this priority level
} PriorityQueue;

typedef struct {
    PriorityQueue queues[MAX_PRIORITY_LEVELS];
    uint32_t current_running;
    uint32_t tick_counter;
    uint32_t quantum_remaining;
    uint32_t last_boost_tick;
    uint32_t active_bitmap;  // Bitmap of non-empty queues
} Scheduler;

// Core process functions
int ProcessInit(void);
uint32_t CreateProcess(void (*entry_point)(void));
uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege);
Process* GetCurrentProcess(void);
Process* GetProcessByPid(uint32_t pid);
void CleanupTerminatedProcesses(void);

// Legacy scheduler functions (can be removed after migration)
void RequestSchedule();
int ShouldSchedule();
void Yield(void);
void ScheduleFromInterrupt(struct Registers* regs);

// New scheduler functions
void InitScheduler(void);
void AddToScheduler(uint32_t slot);
void RemoveFromScheduler(uint32_t slot);
void FastSchedule(struct Registers* regs);
void ProcessBlocked(uint32_t slot);
void DumpSchedulerState(void);

// Security functions
void RegisterSecurityManager(uint32_t pid);
void SecureKernelIntegritySubsystem(void);
void KillProcess(uint32_t pid);
uint64_t GetSystemTicks(void);
void ListProcesses(void);
#endif