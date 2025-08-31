#ifndef PROCESS_H
#define PROCESS_H

#include "stdint.h"
#include "Ipc.h"
#include "Cpu.h"

// =============================================================================
// MLFQ Parameters (last update: 14/08/25)
// =============================================================================

// Core Queue Configuration
#define MAX_PRIORITY_LEVELS 5           // Reduced from 6 - fewer levels = better cache locality
#define RT_PRIORITY_THRESHOLD 3         // Increased RT levels for better interactive response
#define MAX_PROCESSES 64                // Keep as is

// Quantum Management - EXPONENTIAL GROWTH for better differentiation
#define QUANTUM_BASE 4                  // Slightly reduced base for better interactivity
#define QUANTUM_DECAY_SHIFT 1           // Enable quantum growth: L0=4, L1=8, L2=16, L3=32, L4=64
#define QUANTUM_MIN 2                   // Increased minimum to reduce overhead
#define QUANTUM_MAX 64                  // Increased max for CPU-bound processes

// Dynamic Quantum Adjustments - MORE AGGRESSIVE
#define IO_QUANTUM_BOOST_FACTOR 3       // Stronger I/O boost (3/2 = 1.5x)
#define IO_QUANTUM_BOOST_DIVISOR 2
#define CPU_QUANTUM_PENALTY_FACTOR 3    // Stronger CPU penalty (3/4 = 0.75x)
#define CPU_QUANTUM_PENALTY_DIVISOR 4
#define CPU_INTENSIVE_MULTIPLIER 3      // More sensitive detection

// Preemption Control - MORE RESPONSIVE
#define PREEMPTION_BIAS 2               // Reduced - allow more preemption
#define CRITICAL_PREEMPTION_LEVEL 1     // Both levels 0 and 1 can preempt aggressively
#define PREEMPTION_MIN_PRIORITY_GAP 1   // Smaller gap = more responsive

// Fairness and Aging - MUCH MORE AGGRESSIVE
#define AGING_THRESHOLD_BASE 8          // Faster aging trigger (was 15)
#define BOOST_INTERVAL 15               // Much faster global aging (was 40)
#define FAIRNESS_BOOST_INTERVAL 8       // Faster local fairness (was 20)
#define FAIRNESS_BOOST_MULTIPLIER 2     // Reduced multiplier (8*2=16 vs 20*4=80)
#define FAIRNESS_WAIT_THRESHOLD 20      // Faster help for waiting processes (was 50)
#define STARVATION_THRESHOLD 50         // Faster starvation prevention (was 100)

// Load Balancing - MORE ACTIVE
#define LOAD_BALANCE_THRESHOLD 1        // Start balancing earlier
#define LOAD_BALANCE_MULTIPLIER 2       // Threshold = 1*2 = 2 processes
#define HIGH_LOAD_PROCESS_COUNT 4       // Lower threshold for "high load"
#define AGING_ACCELERATION_FACTOR 3     // Faster aging under load

// Process Classification - MORE SENSITIVE
#define IO_BOOST_THRESHOLD 1            // Keep immediate I/O detection
#define IO_BOOST_CONSERVATIVE_MULTIPLIER 1  // More aggressive I/O detection
#define IO_BOOST_AGGRESSIVE_MULTIPLIER 2
#define CPU_BURST_HISTORY 4             // Track more history for better decisions
#define INTERACTIVE_BURST_DIVISOR 2     // More sensitive interactive detection (QUANTUM_BASE/3)
#define INTERACTIVE_AGGRESSIVE_DIVISOR 6 // Very interactive = QUANTUM_BASE/6

// Priority Adjustment Thresholds
#define SINGLE_DEMOTION_ONLY 1          // Keep gradual demotion
#define CPU_INTENSIVE_HISTORY_COUNT 2   // Keep current sensitivity
#define PRIORITY_RESTORE_SYSTEM 1       // Keep system process protection
#define USER_RT_BOOST_THRESHOLD 2       // Easier RT promotion for responsive processes

// Performance and Statistics
#define CONTEXT_SWITCH_OVERHEAD_SAMPLES 8   // Running average sample count (powers of 2)
#define CONTEXT_SWITCH_OVERHEAD_SHIFT 3     // log2(SAMPLES) for bit shifting
#define PERFORMANCE_COUNTER_RESET 10000     // Reset counters every N context switches

// =============================================================================
// Aegis Parameters (carefully, it might blew up)
// =============================================================================
// Security and Process Management
#define TERMINATION_QUEUE_SIZE MAX_PROCESSES    // Size of termination queue
#define CLEANUP_MAX_PER_CALL 3          // Max processes to cleanup per call
#define SECURITY_VIOLATION_LIMIT 3      // Max violations before panic
#define SCHED_CONSISTENCY_INTERVAL 75   // Bitmap check
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

#define PROC_PRIV_SYSTEM     0   // Highest privilege (kernel services)
#define PROC_PRIV_USER       1   // User processes
#define PROC_PRIV_RESTRICTED 2  // Restricted processes

// =============================================================================
// DynamoX Parameters (v0.2)
// =============================================================================
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define FXP_SHIFT 10 // Use 10 bits for the fractional part
#define FXP_SCALE (1 << FXP_SHIFT) // Scaling factor = 1024

#define SMOOTHING_FACTOR 2         // Average over 4 samples (1/4 new, 3/4 old)
#define SAMPLING_INTERVAL 25       // 2x faster sampling (was 50)
#define HZ_PER_PROCESS 50          // More responsive per-process scaling (was 30)
#define QUEUE_PRESSURE_FACTOR 20   // Stronger pressure response
#define QUEUE_PRESSURE_THRESHOLD 2 // Earlier pressure detection (was 3)
#define CS_RATE_THRESHOLD (8 * FXP_SCALE)  // More sensitive high threshold
#define FREQ_BOOST_FACTOR   1331   // Stronger boost: 1.3x (was 1.2x)
#define FREQ_REDUCE_FACTOR  870    // Gentler reduction: 0.85x (was 0.9x)
#define POWER_TURBO_FACTOR  1434   // More aggressive turbo: 1.4x (was 1.3x)
#define HYSTERESIS_THRESHOLD 8     // More responsive changes (was 10)
#define STABILITY_REQ 5            // Confirm stability -- change in Process.c

typedef struct {
    uint64_t magic;
    uint32_t creator_pid;
    uint8_t  privilege;
    uint8_t  flags;
    uint64_t creation_tick;
    uint64_t checksum;
} __attribute__((packed)) SecurityToken;

typedef enum {
    PROC_TERMINATED = 0,  // IMPORTANT: Keep this as 0
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
    char* ProcINFOPath;
} ProcessControlBlock;

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

typedef struct {
    uint64_t timestamp;
    uint16_t process_count;
    uint16_t frequency;
    uint32_t context_switches;
    uint32_t avg_latency;
} FrequencyHistory;

#define FREQ_HISTORY_SIZE 32
#define PREDICTION_WINDOW 10

// Advanced PIT frequency controller
typedef struct {
    FrequencyHistory history[FREQ_HISTORY_SIZE];
    uint32_t history_index;
    uint16_t min_freq;
    uint16_t max_freq;
    uint16_t current_freq;

    // Machine learning-inspired parameters
    float learning_rate;
    float momentum;
    float last_adjustment;

    // Performance metrics
    uint64_t total_idle_ticks;
    uint64_t total_busy_ticks;
    uint32_t missed_deadlines;
    uint32_t power_state;  // 0=low, 1=normal, 2=turbo
} PITController;

// Core process functions
int ProcessInit(void);
uint32_t CreateProcess(void (*entry_point)(void));
ProcessControlBlock* GetCurrentProcess(void);
ProcessControlBlock* GetProcessByPid(uint32_t pid);
void CleanupTerminatedProcesses(void);
void Yield(void);

// New scheduler functions
void InitScheduler(void);
void AddToScheduler(uint32_t slot);
void RemoveFromScheduler(uint32_t slot);
void FastSchedule(Registers* regs);
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