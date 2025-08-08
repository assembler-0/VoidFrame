#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"
#include "Ipc.h"
#include "Cpu.h"


// =============================================================================
// MLFQ SCHEDULER TUNING PARAMETERS
// =============================================================================

// Core Queue Configuration
#define MAX_PRIORITY_LEVELS 6           // Total priority levels (0=highest)
#define RT_PRIORITY_THRESHOLD 1         // Levels 0 to RT_PRIORITY_THRESHOLD-1 are RT
#define MAX_PROCESSES 64                // Maximum concurrent processes

// Quantum Management
#define QUANTUM_BASE 5                  // Base time quantum (ticks)
#define QUANTUM_DECAY_SHIFT 0           // Quantum reduction per level (0 = no decay)
#define QUANTUM_MIN 1                   // Minimum quantum allowed
#define QUANTUM_MAX 32                  // Maximum quantum allowed

// Dynamic Quantum Adjustments
#define IO_QUANTUM_BOOST_FACTOR 5       // Boost factor for I/O processes (n/4)
#define IO_QUANTUM_BOOST_DIVISOR 4
#define CPU_QUANTUM_PENALTY_FACTOR 7    // Penalty factor for CPU hogs (n/8)
#define CPU_QUANTUM_PENALTY_DIVISOR 8
#define CPU_INTENSIVE_MULTIPLIER 2      // Threshold multiplier for CPU intensive

// Preemption Control
#define PREEMPTION_BIAS 4               // Priority difference needed to preempt (higher = less preemption)
#define CRITICAL_PREEMPTION_LEVEL 0     // Only this level can preempt aggressively
#define PREEMPTION_MIN_PRIORITY_GAP 2   // Minimum gap for regular preemption

// Fairness and Aging
#define AGING_THRESHOLD_BASE 15         // Base ticks before aging kicks in
#define BOOST_INTERVAL 40               // Global aging interval (ticks)
#define FAIRNESS_BOOST_INTERVAL 20      // Local fairness boost interval
#define FAIRNESS_BOOST_MULTIPLIER 4     // Multiplier for boost interval (boost every 20*4=80 ticks)
#define FAIRNESS_WAIT_THRESHOLD 50      // Wait time before fairness boost (ticks)
#define STARVATION_THRESHOLD 100        // Critical starvation prevention (ticks)

// Load Balancing
#define LOAD_BALANCE_THRESHOLD 2        // Queue size before load balancing
#define LOAD_BALANCE_MULTIPLIER 3       // Actual threshold = LOAD_BALANCE_THRESHOLD * MULTIPLIER
#define HIGH_LOAD_PROCESS_COUNT 5       // System considered "high load" above this
#define AGING_ACCELERATION_FACTOR 2     // Speed up aging under high load

// Process Classification
#define IO_BOOST_THRESHOLD 1            // I/O operations before considering I/O bound
#define IO_BOOST_CONSERVATIVE_MULTIPLIER 2  // Conservative I/O boost (threshold * 2)
#define IO_BOOST_AGGRESSIVE_MULTIPLIER 3    // Aggressive I/O boost (threshold * 3)
#define CPU_BURST_HISTORY 3             // Number of CPU bursts to track
#define INTERACTIVE_BURST_DIVISOR 2     // CPU burst < QUANTUM_BASE/4 = interactive
#define INTERACTIVE_AGGRESSIVE_DIVISOR 8 // CPU burst < QUANTUM_BASE/8 = very interactive

// Priority Adjustment Thresholds
#define SINGLE_DEMOTION_ONLY 1          // Only demote one level at a time
#define CPU_INTENSIVE_HISTORY_COUNT 2   // Require N consecutive CPU intensive bursts
#define PRIORITY_RESTORE_SYSTEM 1       // Always restore system processes to base
#define USER_RT_BOOST_THRESHOLD 4       // User processes boost to RT_PRIORITY_THRESHOLD

// Performance and Statistics
#define CONTEXT_SWITCH_OVERHEAD_SAMPLES 8   // Running average sample count (powers of 2)
#define CONTEXT_SWITCH_OVERHEAD_SHIFT 3     // log2(SAMPLES) for bit shifting
#define PERFORMANCE_COUNTER_RESET 10000     // Reset counters every N context switches

// Security and Process Management
#define TERMINATION_QUEUE_SIZE MAX_PROCESSES    // Size of termination queue
#define CLEANUP_MAX_PER_CALL 3          // Max processes to cleanup per call
#define SECURITY_VIOLATION_LIMIT 5      // Max violations before panic

// Stack and Memory
#define STACK_SIZE 4096                 // Process stack size
#define CACHE_LINE_SIZE 64              // CPU cache line size for alignment

// =============================================================================
// DERIVED CONSTANTS (Don't modify these - they're calculated from above)
// =============================================================================
#define RT_QUEUE_MASK ((1U << RT_PRIORITY_THRESHOLD) - 1)
#define REGULAR_QUEUE_MASK (~RT_QUEUE_MASK)
#define FAIRNESS_BOOST_ACTUAL_INTERVAL (FAIRNESS_BOOST_INTERVAL * FAIRNESS_BOOST_MULTIPLIER)
#define LOAD_BALANCE_ACTUAL_THRESHOLD (LOAD_BALANCE_THRESHOLD * LOAD_BALANCE_MULTIPLIER)

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
typedef Registers ProcessContext;

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
uint64_t GetSystemTicks(void);
void ListProcesses(void);
void GetProcessStats(uint32_t pid, uint32_t* cpu_time, uint32_t* io_ops, uint32_t* preemptions);
void BoostProcessPriority(uint32_t pid);
void KillProcess(uint32_t pid);

// DEBUG
void DumpPerformanceStats(void);
#endif