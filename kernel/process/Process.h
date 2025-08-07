#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"
#include "Ipc.h"
#include "Cpu.h"

#define MAX_PROCESSES 64
#define STACK_SIZE 4096

// MLFQ Tuning Parameters - Adjusted for fairness
#define MAX_PRIORITY_LEVELS 8
#define QUANTUM_BASE 20          // Larger base quantum for fairness
#define BOOST_INTERVAL 50        // More frequent boosting to prevent starvation
#define RT_PRIORITY_THRESHOLD 2  // Real-time priority threshold
#define IO_BOOST_THRESHOLD 2     // I/O operations to trigger boost
#define CPU_BURST_HISTORY 4      // Track last N CPU bursts for prediction
#define AGING_THRESHOLD_BASE 30  // More aggressive aging for fairness
#define PREEMPTION_BIAS 0        // No RT bias for fairness
#define QUANTUM_DECAY_SHIFT 1    // Quantum decay rate
#define LOAD_BALANCE_THRESHOLD 1 // Better load balancing
#define FAIRNESS_BOOST_INTERVAL 25 // Boost starved processes every 25 ticks

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
    uint8_t base_priority;      // Original priority for reset
    uint8_t is_user_mode;
    uint8_t privilege_level;
    uint32_t cpu_burst_history[CPU_BURST_HISTORY]; // Track CPU usage patterns
    uint32_t io_operations;     // Count of I/O operations
    uint32_t preemption_count;  // Times preempted
    uint64_t cpu_time_accumulated;
    uint64_t last_scheduled_tick;
    uint64_t wait_time;         // Time spent waiting
    TerminationReason term_reason;
    uint32_t exit_code;
    uint64_t termination_time;
    uint32_t parent_pid;
    SecurityToken token;
    MessageQueue ipc_queue;
    ProcessContext context;
    SchedulerNode* scheduler_node;
    uint64_t creation_time;
} Process;

typedef struct {
    SchedulerNode* head;
    SchedulerNode* tail;
    uint32_t count;
    uint32_t quantum;           // Time quantum for this priority level
    uint32_t total_wait_time;   // Aggregate wait time for aging
    uint32_t avg_cpu_burst;     // Average CPU burst for this level
} PriorityQueue;

typedef struct {
    PriorityQueue queues[MAX_PRIORITY_LEVELS];
    uint32_t current_running;
    uint32_t tick_counter;
    uint32_t quantum_remaining;
    uint32_t last_boost_tick;
    uint32_t active_bitmap;     // Bitmap of non-empty queues
    uint32_t rt_bitmap;         // Bitmap of real-time queues
    uint32_t total_processes;   // Total active processes
    uint64_t load_average;      // System load average
    uint32_t context_switch_overhead; // Measured overhead
} Scheduler;

// Core process functions
int ProcessInit(void);
uint32_t CreateProcess(void (*entry_point)(void));
Process* GetCurrentProcess(void);
Process* GetProcessByPid(uint32_t pid);
void CleanupTerminatedProcesses(void);

// Legacy scheduler functions (can be removed after migration)
void RequestSchedule();
int ShouldSchedule();
void Yield(void);
void ScheduleFromInterrupt(Registers* regs);

// New scheduler functions
void InitScheduler(void);
void AddToScheduler(uint32_t slot);
void RemoveFromScheduler(uint32_t slot);
void FastSchedule(struct Registers* regs);
void ProcessBlocked(uint32_t slot);
void DumpSchedulerState(void);

// Security functions
void RegisterSecurityManager(uint32_t pid);
void KillProcess(uint32_t pid);
uint64_t GetSystemTicks(void);
void ListProcesses(void);
void GetProcessStats(uint32_t pid, uint32_t* cpu_time, uint32_t* io_ops, uint32_t* preemptions);
void BoostProcessPriority(uint32_t pid);
#endif