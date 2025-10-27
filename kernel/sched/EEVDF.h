#ifndef VF_EEVDF_SCHED_H
#define VF_EEVDF_SCHED_H

#include "Ipc.h"
#include "Shared.h"
#include "stdint.h"
#include "x64.h"
#ifdef VF_CONFIG_USE_CERBERUS
#include "Cerberus.h"
#endif

// =============================================================================
// EEVDF Parameters - Earliest Eligible Virtual Deadline First
// =============================================================================

// Core Scheduler Configuration
#define EEVDF_MAX_PROCESSES 64                 // Same as MLFQ for consistency
#define EEVDF_MIN_GRANULARITY 750000          // 0.75ms minimum time slice in ns
#define EEVDF_TARGET_LATENCY 6000000          // 6ms target latency in ns
#define EEVDF_WAKEUP_GRANULARITY 1000000      // 1ms wakeup granularity in ns

// Nice level constants
#define EEVDF_NICE_WIDTH 4                     // Nice level width (log2)
#define EEVDF_NICE_0_LOAD 1024                // Load weight for nice 0
#define EEVDF_MIN_NICE (-20)
#define EEVDF_MAX_NICE 19
#define EEVDF_DEFAULT_NICE 0

// Virtual time constants
#define EEVDF_TIME_SLICE_NS (4 * 1000000)     // 4ms default time slice
#define EEVDF_MAX_TIME_SLICE_NS (100 * 1000000) // 100ms max time slice
#define EEVDF_MIN_TIME_SLICE_NS (100000)      // 0.1ms min time slice

// Load balancing and migration
#define EEVDF_MIGRATION_COST_NS 500000        // 0.5ms migration cost
#define EEVDF_LOAD_BALANCE_INTERVAL 5000000   // 5ms between load balance attempts

// Preemption control
#define EEVDF_PREEMPT_THRESHOLD_NS 1000000    // 1ms preemption threshold
#define EEVDF_YIELD_GRANULARITY_NS 100000     // 0.1ms yield granularity

// Security and Process Management (same as MLFQ)
#define EEVDF_TERMINATION_QUEUE_SIZE EEVDF_MAX_PROCESSES
#define EEVDF_CLEANUP_MAX_PER_CALL 3
#define EEVDF_SECURITY_VIOLATION_LIMIT 3
#define EEVDF_STACK_SIZE 4096
#define EEVDF_CACHE_LINE_SIZE 64

// Process privilege levels (same as MLFQ)
#define EEVDF_PROC_PRIV_SYSTEM     PROC_PRIV_SYSTEM
#define EEVDF_PROC_PRIV_NORM       PROC_PRIV_NORM
#define EEVDF_PROC_PRIV_RESTRICTED PROC_PRIV_RESTRICTED

// Security flags (now granular capabilities)
#define EEVDF_CAP_NONE          0ULL
#define EEVDF_CAP_SYS_ADMIN     (1ULL << 0)  // Full system control
#define EEVDF_CAP_NET_ADMIN     (1ULL << 1)  // Network administration
#define EEVDF_CAP_NET_RAW       (1ULL << 2)  // Raw socket access
#define EEVDF_CAP_FILE_READ     (1ULL << 3)  // Read any file
#define EEVDF_CAP_FILE_WRITE    (1ULL << 4)  // Write any file
#define EEVDF_CAP_IPC_OWNER     (1ULL << 5)  // Own IPC objects
#define EEVDF_CAP_KILL          (1ULL << 6)  // Send signals to other processes
#define EEVDF_CAP_SETUID        (1ULL << 7)  // Change UID
#define EEVDF_CAP_SETGID        (1ULL << 8)  // Change GID
#define EEVDF_CAP_SYS_NICE      (1ULL << 9)  // Change process nice value
#define EEVDF_CAP_SYS_RESOURCE  (1ULL << 10) // Modify resource limits
#define EEVDF_CAP_SYS_MODULE    (1ULL << 11) // Load/unload kernel modules
#define EEVDF_CAP_IMMUNE        (1ULL << 12) // Process cannot be killed by non-system processes
#define EEVDF_CAP_CRITICAL      (1ULL << 13) // Process is critical for system operation
#define EEVDF_CAP_SUPERVISOR    (1ULL << 14) // Process runs with supervisor privileges
#define EEVDF_CAP_CORE          (EEVDF_CAP_IMMUNE | EEVDF_CAP_SUPERVISOR | EEVDF_CAP_CRITICAL)

// Nice-to-weight conversion table (based on Linux CFS)
extern const uint32_t eevdf_nice_to_weight[40];
extern const uint32_t eevdf_nice_to_wmult[40];

// EEVDF-specific structures
typedef struct {
    uint64_t magic;
    uint32_t creator_pid;
    uint8_t  privilege;
    uint64_t capabilities; // Renamed from 'flags' to be more explicit
    uint64_t creation_tick;
    uint64_t checksum;     // Checksum for the token itself
    uint64_t pcb_hash;     // Hash for critical PCB fields
} __attribute__((packed)) EEVDFSecurityToken;

// Use the same structure for context switching to avoid mismatches
typedef Registers EEVDFProcessContext;

// Red-black tree node for the EEVDF runqueue
typedef struct EEVDFRBNode {
    struct EEVDFRBNode* left;
    struct EEVDFRBNode* right;
    struct EEVDFRBNode* parent;
    uint8_t color;  // 0 = black, 1 = red
    uint32_t slot;  // Index into process array
} EEVDFRBNode;

// EEVDF Process Control Block
typedef struct {
    char name[64];
    uint32_t pid;
    ProcessState state;
    void* stack;
    
    // Scheduling fields
    int8_t nice;                        // Nice level (-20 to +19)
    uint32_t weight;                    // Scheduling weight (from nice level)
    uint32_t inv_weight;                // Inverse weight for calculations
    
    // Virtual time tracking
    uint64_t vruntime;                  // Virtual runtime (key for ordering)
    uint64_t deadline;                  // Virtual deadline
    uint64_t slice_ns;                  // Current time slice length
    uint64_t exec_start;                // When this task started executing
    uint64_t sum_exec_runtime;          // Total execution time
    
    // Statistics
    uint64_t wait_start;                // When task started waiting
    uint64_t wait_sum;                  // Total wait time
    uint64_t sleep_start;               // When task started sleeping
    uint64_t sleep_sum;                 // Total sleep time
    uint64_t last_wakeup;               // Last wakeup time
    
    // Tree management
    EEVDFRBNode* rb_node;               // Red-black tree node
    
    // Security and system fields (same as MLFQ)
    uint8_t privilege_level;
    uint32_t io_operations;
    uint32_t preemption_count;
    uint64_t cpu_time_accumulated;
    uint64_t creation_time;
    uint32_t parent_pid;
    TerminationReason term_reason;
    uint32_t exit_code;
    uint64_t termination_time;
    
    EEVDFSecurityToken token;
    MessageQueue ipc_queue;
    EEVDFProcessContext context;
    char ProcessRuntimePath[256];
} EEVDFProcessControlBlock;

// EEVDF Runqueue (per-CPU, but we have single CPU)
typedef struct {
    // Red-black tree root for runnable tasks
    EEVDFRBNode* rb_root;
    EEVDFRBNode* rb_leftmost;           // Leftmost node (minimum vruntime)
    
    // Virtual time tracking
    uint64_t min_vruntime;              // Minimum virtual runtime
    uint64_t clock;                     // Runqueue clock
    uint64_t clock_task;                // Per-task clock
    
    // Load tracking
    uint32_t load_weight;               // Total weight of runnable tasks
    uint32_t nr_running;                // Number of runnable tasks
    
    // Current running task
    uint32_t current_slot;              // Currently running task slot
    
    // Statistics
    uint64_t exec_clock;                // Execution clock
    uint64_t avg_idle;                  // Average idle time
    uint64_t avg_vruntime;              // Average vruntime
} EEVDFRunqueue;

// Main EEVDF scheduler structure
typedef struct {
    EEVDFRunqueue rq;                   // Main runqueue
    uint32_t total_processes;           // Total active processes
    uint64_t tick_counter;              // Global tick counter
    uint32_t context_switch_overhead;   // Measured context switch overhead
    
    // Load balancing (future multi-CPU support)
    uint64_t next_balance;              // Next load balance time
    uint64_t load_balance_interval;     // Load balance interval
    
    // Preemption control
    uint64_t last_preempt_check;        // Last preemption check time
    uint32_t preempt_count;             // Number of preemptions
    
    // Performance counters
    uint64_t schedule_count;            // Number of schedule() calls  
    uint64_t switch_count;              // Number of context switches
    uint64_t migration_count;           // Number of task migrations
} EEVDFScheduler;

// Function declarations

// Core scheduler functions
int EEVDFSchedInit(void);
uint32_t EEVDFCreateProcess(const char* name, void (*entry_point)(void));
uint32_t EEVDFCreateSecureProcess(const char* name, void (*entry_point)(void), uint8_t priv, uint64_t capabilities);
EEVDFProcessControlBlock* EEVDFGetCurrentProcess(void);
EEVDFProcessControlBlock* EEVDFGetCurrentProcessByPID(uint32_t pid);
void EEVDFYield(void);
void EEVDFSchedule(Registers* regs);
void EEVDFKillProcess(uint32_t pid);
void EEVDFKillCurrentProcess(const char* reason);

// Time management
uint64_t EEVDFGetSystemTicks(void);
uint64_t EEVDFGetNanoseconds(void);
void EEVDFUpdateClock(EEVDFRunqueue* rq);

// Virtual time functions
uint64_t EEVDFCalcDelta(uint64_t delta_exec, uint32_t weight, uint32_t lw);
void EEVDFUpdateCurr(EEVDFRunqueue* rq, EEVDFProcessControlBlock* curr);
uint64_t EEVDFCalcSlice(EEVDFRunqueue* rq, EEVDFProcessControlBlock* se);

// Tree management
void EEVDFEnqueueTask(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p);
void EEVDFDequeueTask(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p);
EEVDFProcessControlBlock* EEVDFPickNext(EEVDFRunqueue* rq);

// Preemption and yielding
int EEVDFCheckPreempt(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p);
void EEVDFYieldTask(EEVDFRunqueue* rq);

// Wakeup handling
void EEVDFWakeupTask(EEVDFProcessControlBlock* p);
void EEVDFProcessBlocked(uint32_t slot);

// Process management
void EEVDFCleanupTerminatedProcess(void);

// Statistics and debugging
void EEVDFDumpSchedulerState(void);
void EEVDFListProcesses(void);
void EEVDFGetProcessStats(uint32_t pid, uint32_t* cpu_time, uint32_t* wait_time, uint32_t* preemptions);
void EEVDFDumpPerformanceStats(void);

// Nice level functions
void EEVDFSetTaskNice(EEVDFProcessControlBlock* p, int nice);
uint32_t EEVDFNiceToWeight(int nice);
uint32_t EEVDFNiceToWmult(int nice);

#endif // VF_EEVDF_SCHED_H