#include "Process.h"
#include "Atomics.h"
#include "Console.h"
#include "Cpu.h"
#include "Io.h"
#include "Ipc.h"
#include "MemOps.h"
#include "Panic.h"
#include "Pic.h"
#include "Serial.h"
#include "Shell.h"
#include "Spinlock.h"
#include "VMem.h"
#include "stdbool.h"
#include "stdlib.h"

#define offsetof(type, member) ((uint64_t)&(((type*)0)->member))

// Security flags
#define PROC_FLAG_IMMUNE        (1U << 0)
#define PROC_FLAG_CRITICAL      (1U << 1)
#define PROC_FLAG_SUPERVISOR    (1U << 3)
#define PROC_FLAG_CORE          (PROC_FLAG_IMMUNE | PROC_FLAG_SUPERVISOR | PROC_FLAG_CRITICAL)
#define PROC_FLAG_USER

// Performance optimizations
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)
#define CACHE_LINE_SIZE         64
#define ALIGNED_CACHE           __attribute__((aligned(CACHE_LINE_SIZE)))

static const uint64_t SECURITY_MAGIC = 0x5EC0DE4D41474943ULL;
static const uint64_t SECURITY_SALT = 0xDEADBEEFCAFEBABEULL;
static const uint32_t MAX_SECURITY_VIOLATIONS = SECURITY_VIOLATION_LIMIT;

static Process processes[MAX_PROCESSES] ALIGNED_CACHE;
static volatile uint32_t next_pid = 1;
static uint64_t pid_bitmap[MAX_PROCESSES / 64 + 1] = {0};  // Track used PIDs
static irq_flags_t pid_lock = 0;
static volatile uint32_t current_process = 0;
static volatile uint32_t process_count = 0;
static volatile int need_schedule = 0;
static volatile int scheduler_lock = 0;

// Security subsystem
static uint32_t security_manager_pid = 0;
static uint32_t security_violation_count = 0;
static uint64_t last_security_check = 0;
// Fast bitmap operations for process slots (up to 64 processes for 64-bit)
static uint64_t active_process_bitmap = 0;
static uint64_t ready_process_bitmap = 0;

static Scheduler MLFQscheduler ALIGNED_CACHE;
static SchedulerNode scheduler_node_pool[MAX_PROCESSES] ALIGNED_CACHE;
static uint32_t scheduler_node_pool_bitmap[(MAX_PROCESSES + 31) / 32];

// Lockless termination queue using atomic operations
static volatile uint32_t termination_queue[MAX_PROCESSES];
static volatile uint32_t term_queue_head = 0;
static volatile uint32_t term_queue_tail = 0;
static volatile uint32_t term_queue_count = 0;

// Performance counters
static uint64_t context_switches = 0;
static uint64_t scheduler_calls = 0;

extern uint16_t PIT_FREQUENCY_HZ;

static int FastFFS(const uint64_t value) {
    return __builtin_ctzll(value);
}

static int FastCLZ(const uint64_t value) {
    return __builtin_clzll(value);
}

static uint64_t SecureHash(const void* data, const uint64_t len, uint64_t salt) {
    const uint8_t* bytes = data;
    uint64_t hash = salt;

    for (uint64_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL; // FNV-1a prime
    }

    return hash;
}

static uint64_t CalculateSecureChecksum(const SecurityToken* token, uint32_t pid) {
    uint64_t base_hash = SecureHash(token, offsetof(SecurityToken, checksum), SECURITY_SALT);
    uint64_t pid_hash = SecureHash(&pid, sizeof(pid), SECURITY_SALT);
    return base_hash ^ pid_hash;
}

static inline int FindFreeSlotFast(void) {
    if (UNLIKELY(active_process_bitmap == ~1ULL)) { // All slots except 0 taken
        return -1;
    }
    
    // Find first zero bit (skip bit 0 for idle process)
    uint64_t available = ~active_process_bitmap;
    available &= ~1ULL; // Clear bit 0
    
    if (UNLIKELY(available == 0)) {
        return -1;
    }
    
    int slot = FastFFS(available);
    active_process_bitmap |= (1ULL << slot);
    return slot;
}

static inline void FreeSlotFast(int slot) {
    if (LIKELY(slot > 0 && slot < 64)) {
        active_process_bitmap &= ~(1ULL << slot);
    }
}

static void AddToTerminationQueueAtomic(uint32_t slot) {
    uint32_t tail = term_queue_tail;
    uint32_t new_tail = (tail + 1) % MAX_PROCESSES;

    if (UNLIKELY(term_queue_count >= MAX_PROCESSES)) {
        PANIC("Termination queue overflow");
    }

    termination_queue[tail] = slot;
    __sync_synchronize(); // Memory barrier
    term_queue_tail = new_tail;
    AtomicInc(&term_queue_count);
}

static uint32_t RemoveFromTerminationQueueAtomic(void) {
    if (UNLIKELY(term_queue_count == 0)) {
        return MAX_PROCESSES;
    }

    uint32_t head = term_queue_head;
    uint32_t slot = termination_queue[head];

    term_queue_head = (head + 1) % MAX_PROCESSES;
    AtomicDec(&term_queue_count);

    return slot;
}


uint64_t GetSystemTicks(void) {
    return MLFQscheduler.tick_counter;
}

static int ValidateToken(const SecurityToken* token, uint32_t pid_to_check) {
    if (UNLIKELY(!token)) {
        return 0;
    }

    // Constant-time comparison to prevent timing attacks
    uint64_t calculated = CalculateSecureChecksum(token, pid_to_check);
    uint64_t stored = token->checksum;

    // XOR comparison (constant time)
    uint64_t diff = calculated ^ stored;
    uint64_t magic_diff = token->magic ^ SECURITY_MAGIC;

    // Combine all checks
    return (diff | magic_diff) == 0 ? 1 : 0;
}

void TerminateProcess(uint32_t pid, TerminationReason reason, uint32_t exit_code) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    Process* proc = GetProcessByPid(pid);
    if (UNLIKELY(!proc || proc->state == PROC_DYING ||
                 proc->state == PROC_ZOMBIE || proc->state == PROC_TERMINATED)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        return;
    }

    Process* caller = GetCurrentProcess();

    uint32_t slot = proc - processes;

    if (slot >= MAX_PROCESSES) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        return;
    }

    // Enhanced security checks
    if (reason != TERM_SECURITY) {
        // Cross-process termination security
        if (caller->pid != proc->pid) {
            // Only system processes can terminate other processes
            // Check privilege levels - can only kill equal or lower privilege
            if (proc->privilege_level == PROC_PRIV_SYSTEM) {
                // Only system processes can kill system processes
                if (caller->privilege_level != PROC_PRIV_SYSTEM) {
                    SpinUnlockIrqRestore(&scheduler_lock, flags);
                    PrintKernelError("[SECURITY] Process ");
                    PrintKernelInt(caller->pid);
                    PrintKernel(" tried to kill system process ");
                    PrintKernelInt(proc->pid);
                    PrintKernel("\n");
                    TerminateProcess(caller->pid, TERM_SECURITY, 0);
                    return;
                }
            }

            // Cannot terminate immune processes
            if (UNLIKELY(proc->token.flags & PROC_FLAG_IMMUNE)) {
                SpinUnlockIrqRestore(&scheduler_lock, flags);
                TerminateProcess(caller->pid, TERM_SECURITY, 0);
                return;
            }

            // Cannot terminate critical system processes
            if (UNLIKELY(proc->token.flags & PROC_FLAG_CRITICAL)) {
                SpinUnlockIrqRestore(&scheduler_lock, flags);
                TerminateProcess(caller->pid, TERM_SECURITY, 0);
                return;
            }
        }

        // Validate caller's token before allowing termination
        if (UNLIKELY(!ValidateToken(&caller->token, caller->pid))) {
            SpinUnlockIrqRestore(&scheduler_lock, flags);
            TerminateProcess(caller->pid, TERM_SECURITY, 0);
            return;
        }
    }

    // Atomic state transition
    ProcessState old_state = proc->state;
    if (UNLIKELY(AtomicCmpxchg((volatile uint32_t*)&proc->state, old_state, PROC_DYING) != old_state)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        return; // Race condition, another thread is handling termination
    }

    PrintKernel("[SYSTEM] Terminating PID ");
    PrintKernelInt(pid);
    PrintKernel(" Reason: ");
    PrintKernelInt(reason);
    PrintKernel("\n");

    proc->term_reason = reason;
    proc->exit_code = exit_code;
    proc->termination_time = GetSystemTicks();

    // Remove from scheduler
    RemoveFromScheduler(slot);

    // Clear from ready bitmap
    ready_process_bitmap &= ~(1ULL << slot);

    // Request immediate reschedule if current process
    if (UNLIKELY(slot == MLFQscheduler.current_running)) {
        MLFQscheduler.quantum_remaining = 0;
        RequestSchedule();
    }

    proc->state = PROC_ZOMBIE;           // Set state FIRST
    __sync_synchronize();                 // Full memory barrier
    AddToTerminationQueueAtomic(slot);   // Then add to queue
    SpinLock(&pid_lock);
    int idx = proc->pid / 64;
    int bit = proc->pid % 64;
    pid_bitmap[idx] &= ~(1ULL << bit);
    SpinUnlock(&pid_lock);
    // Update scheduler statistics
    if (MLFQscheduler.total_processes > 0) {
        MLFQscheduler.total_processes--;
    }

    SpinUnlockIrqRestore(&scheduler_lock, flags);
}


// AS's deadly termination function - bypasses all protections
static void ASTerminate(uint32_t pid, const char* reason) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    Process* proc = GetProcessByPid(pid);

    if (!proc || proc->state == PROC_TERMINATED) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        return;
    }

    PrintKernelError("[AS] EXECUTING: PID ");
    PrintKernelInt(pid);
    PrintKernelError(" - ");
    PrintKernelError(reason);
    PrintKernelError("\n");

    // AS overrides ALL protections - even immune and critical
    uint32_t slot = proc - processes;
    proc->state = PROC_DYING;
    proc->term_reason = TERM_SECURITY;
    proc->exit_code = 666; // AS signature
    proc->termination_time = GetSystemTicks();

    RemoveFromScheduler(slot);
    ready_process_bitmap &= ~(1ULL << slot);

    if (slot == MLFQscheduler.current_running) {
        MLFQscheduler.quantum_remaining = 0;
        RequestSchedule();
    }

    AddToTerminationQueueAtomic(slot);
    proc->state = PROC_ZOMBIE;

    if (MLFQscheduler.total_processes > 0) {
        MLFQscheduler.total_processes--;
    }

    SpinUnlockIrqRestore(&scheduler_lock, flags);
}

static void SecurityViolationHandler(uint32_t violator_pid, const char* reason) {
    AtomicInc(&security_violation_count);

    PrintKernelError("[AS] Security breach by PID ");
    PrintKernelInt(violator_pid);
    PrintKernelError(": ");
    PrintKernelError(reason);
    PrintKernelError("\n");

    if (UNLIKELY(security_violation_count > MAX_SECURITY_VIOLATIONS)) {
        PANIC("AS: Too many security violations - system compromised");
    }

    ASTerminate(violator_pid, reason);
}


void KillProcess(uint32_t pid) {
    TerminateProcess(pid, TERM_KILLED, 1);
}

void InitSchedulerNodePool(void) {
    FastMemset(scheduler_node_pool, 0, sizeof(scheduler_node_pool));
    FastMemset(scheduler_node_pool_bitmap, 0, sizeof(scheduler_node_pool_bitmap));
}

static SchedulerNode* AllocSchedulerNode(void) {
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        uint32_t word_idx = i / 32;
        uint32_t bit_idx = i % 32;

        if (!(scheduler_node_pool_bitmap[word_idx] & (1U << bit_idx))) {
            scheduler_node_pool_bitmap[word_idx] |= (1U << bit_idx);
            SchedulerNode* node = &scheduler_node_pool[i];
            node->next = NULL;
            node->prev = NULL;
            return node;
        }
    }
    return NULL;  // Pool exhausted
}

static void FreeSchedulerNode(SchedulerNode* node) {
    if (!node) return;

    uint32_t index = node - scheduler_node_pool;
    if (index >= MAX_PROCESSES) return;

    uint32_t word_idx = index / 32;
    uint32_t bit_idx = index % 32;

    scheduler_node_pool_bitmap[word_idx] &= ~(1U << bit_idx);
    node->next = NULL;
    node->prev = NULL;
    node->slot = 0;
}

static inline void EnQueue(PriorityQueue* q, uint32_t slot) {
    SchedulerNode* node = AllocSchedulerNode();
    if (!node) return;  // Pool exhausted

    node->slot = slot;
    processes[slot].scheduler_node = node;

    if (q->tail) {
        q->tail->next = node;
        node->prev = q->tail;
        q->tail = node;
    } else {
        // Empty queue
        q->head = q->tail = node;
    }

    q->count++;
}

static inline uint32_t DeQueue(PriorityQueue* q) {
    if (!q->head) return MAX_PROCESSES;

    SchedulerNode* node = q->head;
    uint32_t slot = node->slot;

    // Remove from front
    q->head = node->next;
    if (q->head) {
        q->head->prev = NULL;
    } else {
        q->tail = NULL;  // Queue became empty
    }

    // Clear process reference and free node
    processes[slot].scheduler_node = NULL;
    FreeSchedulerNode(node);

    q->count--;
    return slot;
}

static inline int QueueEmpty(PriorityQueue* q) {
    return q->count == 0;
}

void InitScheduler(void) {
    FastMemset(&MLFQscheduler, 0, sizeof(Scheduler));

    // Initialize with smart quantum allocation
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        if (i < RT_PRIORITY_THRESHOLD) {
            // Real-time queues get larger quantums
            if (i < RT_PRIORITY_THRESHOLD) {
                MLFQscheduler.queues[i].quantum = QUANTUM_BASE << (RT_PRIORITY_THRESHOLD - i);
                if (MLFQscheduler.queues[i].quantum > QUANTUM_MAX) {
                    MLFQscheduler.queues[i].quantum = QUANTUM_MAX;
                }
            } else {
                MLFQscheduler.queues[i].quantum = QUANTUM_BASE >> ((i - RT_PRIORITY_THRESHOLD) * QUANTUM_DECAY_SHIFT);
                if (MLFQscheduler.queues[i].quantum < QUANTUM_MIN) {
                    MLFQscheduler.queues[i].quantum = QUANTUM_MIN;
                }
            }
            MLFQscheduler.rt_bitmap |= (1U << i);
        } else {
            // Regular queues use exponential decay
            MLFQscheduler.queues[i].quantum = QUANTUM_BASE >> ((i - RT_PRIORITY_THRESHOLD) * QUANTUM_DECAY_SHIFT);
        }
        MLFQscheduler.queues[i].head = NULL;
        MLFQscheduler.queues[i].tail = NULL;
        MLFQscheduler.queues[i].count = 0;
        MLFQscheduler.queues[i].total_wait_time = 0;
        MLFQscheduler.queues[i].avg_cpu_burst = QUANTUM_BASE;
    }

    MLFQscheduler.current_running = 0;
    MLFQscheduler.quantum_remaining = 0;
    MLFQscheduler.active_bitmap = 0;
    MLFQscheduler.last_boost_tick = 0;
    MLFQscheduler.tick_counter = 1;
    MLFQscheduler.total_processes = 0;
    MLFQscheduler.load_average = 0;
    MLFQscheduler.context_switch_overhead = 5; // Initial estimate
}

// Smart process classification and priority assignment
static uint32_t ClassifyProcess(Process* proc) {
    // Real-time system processes get highest priority
    if (proc->privilege_level == PROC_PRIV_SYSTEM && 
        (proc->token.flags & PROC_FLAG_CRITICAL)) {
        return 0;
    }
    
    // I/O bound processes get priority boost
    if (proc->io_operations > IO_BOOST_THRESHOLD) {
        return 1;
    }
    
    // Calculate average CPU burst
    uint32_t avg_burst = 0;
    for (int i = 0; i < CPU_BURST_HISTORY; i++) {
        avg_burst += proc->cpu_burst_history[i];
    }
    avg_burst /= CPU_BURST_HISTORY;
    
    // Short CPU bursts = interactive process = higher priority
    if (avg_burst < QUANTUM_BASE / 2) {
        return 2;
    } else if (avg_burst < QUANTUM_BASE) {
        return 3;
    } else {
        // CPU intensive processes go to lower priorities
        return MAX_PRIORITY_LEVELS - 1;
    }
}

void AddToScheduler(uint32_t slot) {
    if (slot == 0) return;

    Process* proc = &processes[slot];
    if (proc->state != PROC_READY) return;

    uint32_t priority = ClassifyProcess(proc);
    
    // Clamp priority
    if (priority >= MAX_PRIORITY_LEVELS) {
        priority = MAX_PRIORITY_LEVELS - 1;
    }

    proc->priority = priority;
    proc->base_priority = priority; // Remember original
    proc->last_scheduled_tick = MLFQscheduler.tick_counter;
    
    EnQueue(&MLFQscheduler.queues[priority], slot);
    MLFQscheduler.active_bitmap |= (1U << priority);
    MLFQscheduler.total_processes++;
}

// Remove process from scheduler
void RemoveFromScheduler(uint32_t slot) {
    if (slot == 0 || slot >= MAX_PROCESSES) return;

    if (processes[slot].pid == 0) return;

    SchedulerNode* node = processes[slot].scheduler_node;
    if (!node) return;

    uint32_t priority = processes[slot].priority;
    if (priority >= MAX_PRIORITY_LEVELS) return;

    PriorityQueue* q = &MLFQscheduler.queues[priority];

    if (q->count == 0) {
        processes[slot].scheduler_node = NULL;
        return;
    }

    // Remove node from doubly-linked list
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        q->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        q->tail = node->prev;
    }

    node->next = NULL;
    node->prev = NULL;
    node->slot = 0;

    if (q->count > 0) {
        q->count--;
    }
    MLFQscheduler.total_processes--;

    // Update bitmap if queue became empty
    if (q->count == 0) {
        MLFQscheduler.active_bitmap &= ~(1ULL << priority);
        if (priority < RT_PRIORITY_THRESHOLD) {
            MLFQscheduler.rt_bitmap &= ~(1ULL << priority);
        }
        q->head = q->tail = NULL;
    }

    processes[slot].scheduler_node = NULL;
    FreeSchedulerNode(node);
}

static inline int FindBestQueue(void) {
    if (MLFQscheduler.active_bitmap == 0) return -1;

    // Real-time queues always have absolute priority
    uint32_t rt_active = MLFQscheduler.active_bitmap & MLFQscheduler.rt_bitmap;
    if (rt_active) {
        return FastFFS(rt_active);
    }

    // For non-RT queues, consider load balancing
    uint32_t regular_active = MLFQscheduler.active_bitmap & ~MLFQscheduler.rt_bitmap;
    if (!regular_active) return -1;

    // FIXED: Less aggressive load balancing to prevent starvation
    for (int i = RT_PRIORITY_THRESHOLD; i < MAX_PRIORITY_LEVELS; i++) {
        if (regular_active & (1U << i)) {
            PriorityQueue* queue = &MLFQscheduler.queues[i];

            // FIXED: Higher threshold to prevent constant queue hopping
            if (queue->count > LOAD_BALANCE_ACTUAL_THRESHOLD &&
                (regular_active & ~(1U << i))) {
                continue;
            }

            return i;
        }
    }

    // Fallback to first available
    return FastFFS(regular_active);
}

// Smart aging algorithm with selective boosting
static void SmartAging(void) {
    uint64_t current_tick = MLFQscheduler.tick_counter;
    
    // Calculate system load for adaptive aging
    uint32_t total_waiting = 0;
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        total_waiting += MLFQscheduler.queues[i].total_wait_time;
    }
    
    // Adaptive aging threshold based on system load
    uint32_t aging_threshold = AGING_THRESHOLD_BASE;
    if (total_waiting > MLFQscheduler.total_processes * FAIRNESS_WAIT_THRESHOLD) {
        aging_threshold /= AGING_ACCELERATION_FACTOR;
    }

    // Selective process boosting
    for (int level = RT_PRIORITY_THRESHOLD; level < MAX_PRIORITY_LEVELS; level++) {
        PriorityQueue* queue = &MLFQscheduler.queues[level];
        SchedulerNode* node = queue->head;
        
        while (node) {
            SchedulerNode* next = node->next;
            uint32_t slot = node->slot;
            Process* proc = &processes[slot];
            
            uint64_t wait_time = current_tick - proc->last_scheduled_tick;
            
            // Boost processes that have waited too long
            if (wait_time > aging_threshold || wait_time > STARVATION_THRESHOLD) {
                // Remove from current queue
                if (node->prev) {
                    node->prev->next = node->next;
                } else {
                    queue->head = node->next;
                }
                
                if (node->next) {
                    node->next->prev = node->prev;
                } else {
                    queue->tail = node->prev;
                }
                
                queue->count--;
                
                // To prevent starvation, user processes must be boosted
                // to the highest priority (0) to guarantee they get to run. Boosting
                // them to a lower priority was ineffective.
                uint32_t new_priority = 0;

                proc->priority = new_priority;
                proc->last_scheduled_tick = current_tick;

                // Add to higher priority queue
                PriorityQueue* dst = &MLFQscheduler.queues[new_priority];
                node->next = NULL;
                node->prev = dst->tail;

                if (dst->tail) {
                    dst->tail->next = node;
                    dst->tail = node;
                } else {
                    dst->head = dst->tail = node;
                }
                dst->count++;
                MLFQscheduler.active_bitmap |= (1U << new_priority);
            }

            node = next;
        }

        // Update bitmap if queue became empty
        if (queue->count == 0) {
            MLFQscheduler.active_bitmap &= ~(1U << level);
        }
    }
}


static inline int AstraPreflightCheck(uint32_t slot) {
    if (slot == 0) return 1; // Idle process is always safe.

    Process* proc = &processes[slot];

    // This catches unauthorized modifications to the process state or flags.
    if (UNLIKELY(!ValidateToken(&proc->token, proc->pid))) {
        PrintKernelError("[AS-PREFLIGHT] Token validation failed for PID: ");
        PrintKernelInt(proc->pid);
        PrintKernelError("\n");
        ASTerminate(proc->pid, "Pre-flight token validation failure");
        return 0; // Do not schedule this process.
    }

    // If a process is marked as SYSTEM but lacks the SUPERVISOR or CRITICAL flag,
    // it's a huge red flag that it may have tampered with its own privilege level.
    if (UNLIKELY(proc->privilege_level == PROC_PRIV_SYSTEM &&
                 !(proc->token.flags & (PROC_FLAG_SUPERVISOR | PROC_FLAG_CRITICAL | PROC_FLAG_IMMUNE)))) {
        PrintKernelError("[AS-PREFLIGHT] Illicit SYSTized EM privilege detected for PID: ");
        PrintKernelInt(proc->pid);
        PrintKernelError("\n");
        ASTerminate(proc->pid, "Unauthorized privilege escalation");
        return 0; // Do not schedule this process.
    }

    return 1; // Process appears safe to run.
}

// Enhanced scheduler with smart preemption and load balancing
void FastSchedule(struct Registers* regs) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    uint64_t schedule_start = MLFQscheduler.tick_counter;

    AtomicInc(&scheduler_calls);
    AtomicInc(&MLFQscheduler.tick_counter);

    // FIXED: Less frequent fairness boosting to prevent chaos
    if (UNLIKELY(MLFQscheduler.tick_counter % FAIRNESS_BOOST_ACTUAL_INTERVAL == 0)) {
        // Boost processes that haven't run recently
        for (int i = 1; i < MAX_PROCESSES; i++) {
            if (processes[i].pid != 0 && processes[i].state == PROC_READY) {
                uint64_t wait_time = MLFQscheduler.tick_counter - processes[i].last_scheduled_tick;

                // FIXED: Much higher threshold and respect RT boundaries
                if (wait_time > FAIRNESS_WAIT_THRESHOLD || wait_time > STARVATION_THRESHOLD) {
                    if (processes[i].privilege_level == PROC_PRIV_SYSTEM && processes[i].priority > 0) {
                        processes[i].priority = 0; // System processes to RT
                    } else if (processes[i].privilege_level != PROC_PRIV_SYSTEM && processes[i].priority > RT_PRIORITY_THRESHOLD) {
                        processes[i].priority = RT_PRIORITY_THRESHOLD; // User processes to user RT
                    }
                }
            }
        }
    }

    // Smart aging for long-term fairness - FIXED: Less frequent
    if (UNLIKELY(MLFQscheduler.tick_counter - MLFQscheduler.last_boost_tick >= (BOOST_INTERVAL * 2))) {
        SmartAging();
        MLFQscheduler.last_boost_tick = MLFQscheduler.tick_counter;
    }

    uint32_t old_slot = MLFQscheduler.current_running;
    Process* old_proc = &processes[old_slot];
    uint32_t cpu_burst = 0;

    // Handle currently running process
    if (LIKELY(old_slot != 0)) {
        ProcessState state = old_proc->state;

        if (UNLIKELY(state == PROC_DYING || state == PROC_ZOMBIE || state == PROC_TERMINATED)) {
            goto select_next;
        }

        // Calculate CPU burst for this process
        cpu_burst = MLFQscheduler.queues[old_proc->priority].quantum - MLFQscheduler.quantum_remaining;

        // Update CPU burst history
        for (int i = CPU_BURST_HISTORY - 1; i > 0; i--) {
            old_proc->cpu_burst_history[i] = old_proc->cpu_burst_history[i-1];
        }
        old_proc->cpu_burst_history[0] = cpu_burst;
        old_proc->cpu_time_accumulated += cpu_burst;

        FastMemcpy(&old_proc->context, regs, sizeof(struct Registers));

        if (LIKELY(MLFQscheduler.quantum_remaining > 0)) {
            MLFQscheduler.quantum_remaining--;
        }

        // FIXED: Much less aggressive preemption logic
        int best_priority = FindBestQueue();
        bool should_preempt = false;

        // FIXED: Higher bias and only for critical RT processes
        if (best_priority == CRITICAL_PREEMPTION_LEVEL &&
            old_proc->priority > PREEMPTION_MIN_PRIORITY_GAP) { // And only if current is much lower priority
            should_preempt = true;
        }
        // FIXED: Only preempt on quantum expiry or significantly higher priority
        else if (MLFQscheduler.quantum_remaining == 0 ||
                (best_priority != -1 && (best_priority + PREEMPTION_BIAS < (int)old_proc->priority))) {
            should_preempt = true;
        }

        if (!should_preempt) {
            SpinUnlockIrqRestore(&scheduler_lock, flags);
            return;
        }

        // Prepare for context switch
        old_proc->state = PROC_READY;
        ready_process_bitmap |= (1ULL << old_slot);
        old_proc->preemption_count++;

        // FIXED: Much less aggressive priority adjustment
        if (old_proc->privilege_level != PROC_PRIV_SYSTEM) {
            // Only demote if process used full quantum AND is CPU intensive
            if (MLFQscheduler.quantum_remaining == 0) { // Simpler check: used its whole turn
                if (old_proc->priority < MAX_PRIORITY_LEVELS - 1) {
                    old_proc->priority++; // Demote CPU-bound tasks
                }
            }
            // Boost truly interactive processes that yielded early
            else if (cpu_burst < (MLFQscheduler.queues[old_proc->priority].quantum / 2)) {
                // Boost to the highest user priority if it's not already there.
                if (old_proc->priority > RT_PRIORITY_THRESHOLD) {
                    old_proc->priority = RT_PRIORITY_THRESHOLD;
                }
            }
        } else {
            // System processes: restore to base priority if demoted
            if (old_proc->priority > old_proc->base_priority) {
                old_proc->priority = old_proc->base_priority;
            }
        }

        // Re-add the process to the scheduler with its new (or old) priority
        AddToScheduler(old_slot);
    }

select_next:;
    int next_priority = FindBestQueue();
    uint32_t next_slot;

    if (UNLIKELY(next_priority == -1)) {
        next_slot = 0; // Nothing is ready, select idle process.
    } else {
        next_slot = DeQueue(&MLFQscheduler.queues[next_priority]);

        if (UNLIKELY(!AstraPreflightCheck(next_slot))) {
            goto select_next;
        }

        if (UNLIKELY(next_slot >= MAX_PROCESSES || processes[next_slot].state != PROC_READY)) {
            goto select_next;
        }
    }

    // Context switch with performance tracking
    MLFQscheduler.current_running = next_slot;
    current_process = next_slot;

    if (LIKELY(next_slot != 0)) {
        Process* new_proc = &processes[next_slot];
        new_proc->state = PROC_RUNNING;
        ready_process_bitmap &= ~(1ULL << next_slot);

        // FIXED: Always reset to full quantum for fairness
        uint32_t base_quantum = MLFQscheduler.queues[new_proc->priority].quantum;

        // FIXED: Less aggressive quantum adjustment
        if (new_proc->io_operations >= IO_BOOST_THRESHOLD * 3) {
            base_quantum = (base_quantum * IO_QUANTUM_BOOST_FACTOR) / IO_QUANTUM_BOOST_DIVISOR;
        }

        // FIXED: Less punishment for CPU intensive processes
        uint32_t avg_burst = 0;
        for (int i = 0; i < CPU_BURST_HISTORY; i++) {
            avg_burst += new_proc->cpu_burst_history[i];
        }
        avg_burst /= CPU_BURST_HISTORY;

        if (avg_burst > base_quantum * CPU_INTENSIVE_MULTIPLIER) {
            base_quantum = (base_quantum * CPU_QUANTUM_PENALTY_FACTOR) / CPU_QUANTUM_PENALTY_DIVISOR;
        }

        MLFQscheduler.quantum_remaining = base_quantum;
        new_proc->last_scheduled_tick = MLFQscheduler.tick_counter;

        FastMemcpy(regs, &new_proc->context, sizeof(struct Registers));
        AtomicInc(&context_switches);

        // Update context switch overhead measurement
        uint32_t overhead = MLFQscheduler.tick_counter - schedule_start;
        MLFQscheduler.context_switch_overhead = (MLFQscheduler.context_switch_overhead * 7 + overhead) / 8;
    } else {
        MLFQscheduler.quantum_remaining = 0;
    }

    SpinUnlockIrqRestore(&scheduler_lock, flags);
}

void ProcessBlocked(uint32_t slot) {
    Process* proc = &processes[slot];

    // Track I/O operations for classification
    proc->io_operations++;

    if (slot == MLFQscheduler.current_running) {
        // Calculate partial CPU burst
        uint32_t partial_burst = MLFQscheduler.queues[proc->priority].quantum - MLFQscheduler.quantum_remaining;

        // Update burst history with partial burst
        for (int i = CPU_BURST_HISTORY - 1; i > 0; i--) {
            proc->cpu_burst_history[i] = proc->cpu_burst_history[i-1];
        }
        proc->cpu_burst_history[0] = partial_burst;

        MLFQscheduler.quantum_remaining = 0;
        RequestSchedule();
    }

    // FIXED: Much more conservative I/O boosting
    if (proc->state == PROC_READY && proc->privilege_level != PROC_PRIV_SYSTEM) {
        // If a process blocks, it's very likely interactive. Boost it.
        // We will boost it to the highest user-space priority.
        uint32_t highest_user_priority = RT_PRIORITY_THRESHOLD;

        if (proc->priority > highest_user_priority) {
            // Remove from the old, lower-priority queue
            if (proc->scheduler_node) {
                RemoveFromScheduler(slot);
            }

            // Boost priority directly
            proc->priority = highest_user_priority;

            // Add it back to the new, higher-priority queue
            AddToScheduler(slot);
        }
    }
}

int ShouldSchedule(void) {
    if (need_schedule) {
        need_schedule = 0;
        return 1;
    }
    return 0;
}

void RequestSchedule(void) {
    need_schedule = 1;
}

void Yield() {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    Process* current = GetCurrentProcess();
    if (current) {
        // A process that yields is ready to run again, just giving up its timeslice.
        // Setting it to PROC_BLOCKED was incorrect as there was no corresponding unblock mechanism.
        current->state = PROC_READY;
    }
    RequestSchedule();
    // This instruction halts the CPU until the next interrupt (e.g., the timer),
    // which will then trigger the scheduler.
    SpinUnlockIrqRestore(&scheduler_lock, flags);
    __asm__ __volatile__("hlt");
}

void ProcessExitStub() {
    Process* current = GetCurrentProcess();

    PrintKernelWarning("\n[SYSTEM] Process PID ");
    PrintKernelInt(current->pid);
    PrintKernelWarning(" exited normally\n");

    // Use the safe termination function
    TerminateProcess(current->pid, TERM_NORMAL, 0);
    // Should not reach here, but just in case
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege, uint32_t initial_flags) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    if (UNLIKELY(!entry_point)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: NULL entry point");
    }

    Process* creator = GetCurrentProcess();

    if (privilege == PROC_PRIV_SYSTEM && creator->privilege_level != PROC_PRIV_SYSTEM) {
        PrintKernelError("[AS-API] Unauthorized privilege escalation attempt by PID: ");
        PrintKernelInt(creator->pid);
        PrintKernelError(" (tried to create a system process).\n");
        // This is a hostile act. Terminate the caller.
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        ASTerminate(creator->pid, "Illegal attempt to create system process");
        return 0; // Return PID 0 to indicate failure
    }


    // Enhanced security validation
    if (UNLIKELY(!ValidateToken(&creator->token, creator->pid))) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        SecurityViolationHandler(creator->pid, "Corrupt token during process creation");
        return 0;
    }

    // Privilege escalation check
    if (privilege == PROC_PRIV_SYSTEM) {
        if (UNLIKELY(creator->pid != 0 && creator->privilege_level != PROC_PRIV_SYSTEM)) {
            SpinUnlockIrqRestore(&scheduler_lock, flags);
            SecurityViolationHandler(creator->pid, "Unauthorized system process creation");
            return 0;
        }
    }

    if (UNLIKELY(process_count >= MAX_PROCESSES)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: Too many processes");
    }

    // Fast slot allocation
    int slot = FindFreeSlotFast();
    if (UNLIKELY(slot == -1)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: No free process slots");
    }

    uint32_t new_pid = 0;
    SpinLock(&pid_lock);
    for (int i = 1; i < MAX_PROCESSES; i++) {
        int idx = i / 64;
        int bit = i % 64;
        if (!(pid_bitmap[idx] & (1ULL << bit))) {
            pid_bitmap[idx] |= (1ULL << bit);
            new_pid = i;
            break;
        }
    }
    SpinUnlock(&pid_lock);

    if (new_pid == 0) {
        FreeSlotFast(slot);
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: PID exhaustion");
    }

    // Clear slot securely
    FastMemset(&processes[slot], 0, sizeof(Process));

    // Allocate aligned stack
    void* stack = VMemAllocWithGuards(STACK_SIZE);
    if (UNLIKELY(!stack)) {
        FreeSlotFast(slot);
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: Failed to allocate stack");
    }

    // Initialize process with enhanced security and scheduling data

    processes[slot].pid = new_pid;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].privilege_level = privilege;
    processes[slot].priority = (privilege == PROC_PRIV_SYSTEM) ? 0 : RT_PRIORITY_THRESHOLD;
    processes[slot].base_priority = processes[slot].priority;
    processes[slot].is_user_mode = (privilege != PROC_PRIV_SYSTEM);
    processes[slot].scheduler_node = NULL;
    processes[slot].creation_time = GetSystemTicks();
    processes[slot].last_scheduled_tick = GetSystemTicks();
    processes[slot].cpu_time_accumulated = 0;
    processes[slot].io_operations = 0;
    processes[slot].preemption_count = 0;
    processes[slot].wait_time = 0;

    // Initialize CPU burst history with reasonable defaults
    for (int i = 0; i < CPU_BURST_HISTORY; i++) {
        processes[slot].cpu_burst_history[i] = QUANTUM_BASE / 2;
    }

    // Enhanced token initialization
    SecurityToken* token = &processes[slot].token;
    token->magic = SECURITY_MAGIC;
    token->creator_pid = creator->pid;
    token->privilege = privilege;
    token->flags = initial_flags;
    token->creation_tick = GetSystemTicks();
    token->checksum = CalculateSecureChecksum(token, new_pid);

    // Set up secure initial context
    uint64_t rsp = (uint64_t)stack + STACK_SIZE;
    rsp &= ~0xF; // 16-byte alignment

    // Push ProcessExitStub as return address
    rsp -= 8;
    *(uint64_t*)rsp = (uint64_t)ProcessExitStub;

    processes[slot].context.rsp = rsp;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202;
    processes[slot].context.cs = 0x08;
    processes[slot].context.ss = 0x10;

    // Initialize IPC queue
    processes[slot].ipc_queue.head = 0;
    processes[slot].ipc_queue.tail = 0;
    processes[slot].ipc_queue.count = 0;

    // Atomically update counters
    __sync_fetch_and_add(&process_count, 1);
    ready_process_bitmap |= (1ULL << slot);

    // Add to scheduler
    AddToScheduler(slot);

    SpinUnlockIrqRestore(&scheduler_lock, flags);
    return new_pid;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER, 0);
}


void ScheduleFromInterrupt(Registers* regs) {
    FastSchedule(regs);
}

void CleanupTerminatedProcesses(void) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    // Process a limited number per call to avoid long interrupt delays
    int cleanup_count = 0;
    const int MAX_CLEANUP_PER_CALL = CLEANUP_MAX_PER_CALL;

    while (AtomicRead(&term_queue_count) > 0 && cleanup_count < MAX_CLEANUP_PER_CALL) {
        uint32_t slot = RemoveFromTerminationQueueAtomic();
        if (slot >= MAX_PROCESSES) break;

        Process* proc = &processes[slot];

        // Double-check state
        if (proc->state != PROC_ZOMBIE) {
            PrintKernelWarning("[SYSTEM] Cleanup found non-zombie process (PID: ");
            PrintKernelInt(proc->pid);
            PrintKernelWarning(", State: ");
            PrintKernelInt(proc->state);
            PrintKernelWarning(") in termination queue. Skipping.\n");
            continue;
        }

        PrintKernel("[SYSTEM] Cleaning up process PID: ");
        PrintKernelInt(proc->pid);
        PrintKernel("\n");

        // Cleanup resources
        if (proc->stack) {
            VMemFreeWithGuards(proc->stack, STACK_SIZE);
            proc->stack = NULL;
        }

        // Clear IPC queue
        proc->ipc_queue.head = 0;
        proc->ipc_queue.tail = 0;
        proc->ipc_queue.count = 0;

        // Clear process structure - this will set state to PROC_TERMINATED (0)
        uint32_t pid_backup = proc->pid; // Keep for logging
        FastMemset(proc, 0, sizeof(Process));

        // Free the slot
        FreeSlotFast(slot);
        process_count--;
        cleanup_count++;

        PrintKernel("[SYSTEM] Process PID ");
        PrintKernelInt(pid_backup);
        PrintKernel(" cleaned up successfully (state now PROC_TERMINATED=0)\n");
    }
    SpinUnlockIrqRestore(&scheduler_lock, flags);
}

Process* GetCurrentProcess(void) {
    if (current_process >= MAX_PROCESSES) {
        PANIC("GetCurrentProcess: Invalid current process index");
    }
    return &processes[current_process];
}

Process* GetProcessByPid(uint32_t pid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_TERMINATED) {
            return &processes[i];
        }
    }
    return NULL;
}


void DynamoX(void) {
    PrintKernel("[DX] DynamoX v0.2 Enhanced starting...\n");

    typedef struct {
        uint16_t min_freq;
        uint16_t max_freq;
        uint16_t current_freq;
        uint8_t  power_state;
        uint32_t history_index;
        FrequencyHistory history[FREQ_HISTORY_SIZE];

        // Enhanced learning parameters
        int32_t learning_rate;
        int32_t momentum;
        int32_t last_adjustment;
        int32_t prediction_weight;

        // Responsiveness enhancements
        uint32_t emergency_boost_counter;
        uint32_t stability_counter;
        uint16_t predicted_freq;
        uint16_t baseline_freq;

        // Smart adaptation
        uint32_t load_trend;
        uint32_t performance_score;
        uint8_t  adaptive_mode;
        uint32_t consecutive_samples;
    } Controller;

    Controller controller = {
        .min_freq = 200,        // Increased base responsiveness
        .max_freq = 2000,       // Higher ceiling for heavy loads
        .current_freq = PIT_FREQUENCY_HZ,
        .baseline_freq = 400,   // Smart baseline instead of minimum
        .learning_rate = (int32_t)(0.25f * FXP_SCALE),    // More aggressive learning
        .momentum = (int32_t)(0.8f * FXP_SCALE),          // Higher momentum for stability
        .prediction_weight = (int32_t)(0.3f * FXP_SCALE), // Predictive component
        .adaptive_mode = 1,     // Start in balanced adaptive mode
        .last_adjustment = 0,
        .history_index = 0,
        .power_state = 1,
        .performance_score = 50 // Start with neutral score
    };

    // Enhanced tuning parameters
    const uint32_t ENHANCED_SAMPLING = 25;      // 2x faster sampling
    const uint32_t ENHANCED_HZ_PER_PROCESS = 80; // More responsive scaling
    const uint32_t EMERGENCY_CS_THRESHOLD = 15;  // Context switches per tick
    const uint32_t STABILITY_REQUIREMENT = 5;    // Confirm stability
    const uint32_t PREDICTION_CONFIDENCE = 70;   // Minimum confidence %
    const uint32_t ENHANCED_QUEUE_PRESSURE = 30; // Stronger pressure response
    const uint32_t ENHANCED_HYSTERESIS = 5;      // More responsive changes

    uint64_t last_sample_time = GetSystemTicks();
    uint64_t last_context_switches = context_switches;
    uint32_t consecutive_high_load = 0;
    uint32_t consecutive_low_load = 0;

    while (1) {
        CleanupTerminatedProcesses();
        uint64_t current_time = GetSystemTicks();
        uint64_t time_delta = current_time - last_sample_time;

        if (time_delta >= ENHANCED_SAMPLING) {
            // Enhanced process and queue metrics
            int process_count = __builtin_popcountll(active_process_bitmap);
            int ready_count = __builtin_popcountll(ready_process_bitmap);
            uint64_t cs_delta = context_switches - last_context_switches;

            if (time_delta == 0) time_delta = 1; // Avoid division by zero

            // Enhanced load calculations
            uint32_t load = (ready_count * FXP_SCALE) / MAX_PROCESSES;
            uint32_t cs_rate = (cs_delta * FXP_SCALE) / time_delta;

            // Smart queue analysis with RT priority awareness
            uint32_t total_queue_depth = 0;
            uint32_t max_queue_depth = 0;
            uint32_t rt_queue_depth = 0;
            uint32_t active_queues = 0;

            for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
                uint32_t depth = MLFQscheduler.queues[i].count;
                total_queue_depth += depth;
                if (depth > 0) active_queues++;
                if (depth > max_queue_depth) max_queue_depth = depth;
                if (i < RT_PRIORITY_THRESHOLD) rt_queue_depth += depth;
            }

            // Smart baseline calculation
            uint32_t target_freq = controller.baseline_freq;

            // Factor 1: Enhanced process load with RT awareness
            if (process_count > 1) {
                uint32_t base_load = (process_count - 1) * ENHANCED_HZ_PER_PROCESS;
                uint32_t rt_boost = rt_queue_depth * (ENHANCED_HZ_PER_PROCESS / 2);
                target_freq += base_load + rt_boost;
            }

            // Factor 2: Intelligent queue pressure with active queue weighting
            if (max_queue_depth > 2) { // More sensitive threshold
                uint32_t pressure_factor = (active_queues > 2) ?
                    ENHANCED_QUEUE_PRESSURE * 2 : ENHANCED_QUEUE_PRESSURE;
                target_freq += max_queue_depth * pressure_factor;
            }

            // Factor 3: Advanced context switch analysis with emergency response
            if (cs_rate > EMERGENCY_CS_THRESHOLD * FXP_SCALE) {
                // Emergency thrashing response
                target_freq = (target_freq * 1536) >> FXP_SHIFT; // 1.5x emergency boost
                controller.emergency_boost_counter++;
                consecutive_high_load++;
                consecutive_low_load = 0;

                if (controller.emergency_boost_counter > 3) {
                    controller.power_state = 3; // Emergency turbo
                    target_freq = controller.max_freq;
                }

                PrintKernelWarning("[DX] Emergency boost - CS rate: ");
                PrintKernelInt(cs_rate >> FXP_SHIFT);
                PrintKernel("\n");

            } else if (cs_rate > (8 * FXP_SCALE)) { // Enhanced threshold
                target_freq = (target_freq * 1331) >> FXP_SHIFT; // 1.3x boost
                consecutive_high_load++;
                consecutive_low_load = 0;
                controller.emergency_boost_counter = 0;
            } else if (cs_rate < (3 * FXP_SCALE) && process_count > 1) { // Higher low threshold
                target_freq = (target_freq * 870) >> FXP_SHIFT; // 0.85x gentler reduction
                consecutive_low_load++;
                consecutive_high_load = 0;
                controller.emergency_boost_counter = 0;
            } else {
                controller.emergency_boost_counter = 0;
            }

            // Factor 4: Predictive scaling using pattern history
            if (controller.history_index > PREDICTION_WINDOW) {
                uint32_t predicted_cs = 0;
                uint32_t trend_weight = 0;

                for (int i = 1; i <= PREDICTION_WINDOW; i++) {
                    uint32_t idx = (controller.history_index - i) % FREQ_HISTORY_SIZE;
                    predicted_cs += controller.history[idx].context_switches;
                    trend_weight += (PREDICTION_WINDOW - i + 1); // Recent samples weighted more
                }

                predicted_cs = (predicted_cs * trend_weight) /
                              (PREDICTION_WINDOW * (PREDICTION_WINDOW + 1) / 2);

                // Apply prediction if trend suggests increase
                if (predicted_cs > cs_delta + (cs_delta / 5)) { // 20% increase predicted
                    uint16_t prediction_boost = (target_freq * controller.prediction_weight) >> FXP_SHIFT;
                    target_freq += prediction_boost;
                    controller.predicted_freq = target_freq;
                }
            }

            // Factor 5: Enhanced adaptive power management
            uint32_t load_percentage = (total_queue_depth * 100) / MAX_PROCESSES;

            if (consecutive_low_load > 8 && process_count <= 2) {
                controller.power_state = 0; // Deep power saving
                target_freq = controller.min_freq;
                controller.adaptive_mode = 0; // Conservative
            } else if (consecutive_high_load > 4 || load_percentage > 50) {
                controller.power_state = 2; // Performance mode
                target_freq = (target_freq * 1434) >> FXP_SHIFT; // 1.4x turbo
                controller.adaptive_mode = 2; // Aggressive
            } else if (load_percentage > 75 || controller.emergency_boost_counter > 0) {
                controller.power_state = 3; // Maximum performance
                target_freq = (target_freq * 1536) >> FXP_SHIFT; // 1.5x max turbo
                controller.adaptive_mode = 2;
            } else {
                controller.power_state = 1; // Balanced
                controller.adaptive_mode = 1;
            }

            // Enhanced learning with adaptive rates
            int32_t adaptive_learning = controller.learning_rate;
            if (controller.adaptive_mode == 2) {
                adaptive_learning = (controller.learning_rate * 3) >> 1; // 1.5x faster
            } else if (controller.adaptive_mode == 0) {
                adaptive_learning = (controller.learning_rate * 3) >> 2;   // 0.75x slower
            }

            // Apply momentum and learning
            int32_t diff = (int32_t)target_freq - (int32_t)controller.current_freq;
            int32_t adjustment = (diff * adaptive_learning);
            adjustment += (((int64_t)controller.momentum * controller.last_adjustment) >> FXP_SHIFT);

            controller.last_adjustment = adjustment;

            // Smart frequency application with adaptive bounds
            uint16_t new_freq = controller.current_freq + (adjustment >> FXP_SHIFT);

            // Dynamic bounds based on power state
            uint16_t effective_min = (controller.power_state == 0) ?
                controller.min_freq : (controller.min_freq + controller.baseline_freq) / 2;
            uint16_t effective_max = (controller.power_state >= 2) ?
                controller.max_freq : (controller.max_freq * 4) / 5;

            if (new_freq < effective_min) new_freq = effective_min;
            if (new_freq > effective_max) new_freq = effective_max;

            // Enhanced hysteresis with stability consideration
            uint32_t change_threshold = (controller.stability_counter > STABILITY_REQUIREMENT) ?
                ENHANCED_HYSTERESIS / 2 : ENHANCED_HYSTERESIS;

            if (ABSi(new_freq - controller.current_freq) > change_threshold) {
                PitSetFrequency(new_freq);
                controller.current_freq = new_freq;
                controller.stability_counter = 0;

                // Performance feedback scoring
                if (cs_rate < (3 * FXP_SCALE)) {
                    controller.performance_score = MIN(100, controller.performance_score + 1);
                } else if (cs_rate > (8 * FXP_SCALE)) {
                    controller.performance_score = MAX(0, controller.performance_score - 1);
                }
            } else {
                controller.stability_counter++;
            }

            // Enhanced history recording with more context
            uint32_t idx = controller.history_index % FREQ_HISTORY_SIZE;
            controller.history[idx] = (FrequencyHistory){
                .timestamp = current_time,
                .process_count = process_count,
                .frequency = controller.current_freq,
                .context_switches = cs_delta,
                .avg_latency = total_queue_depth | (rt_queue_depth << 8) | (controller.power_state << 16)
            };
            controller.history_index++;
            controller.consecutive_samples++;

            // Periodic detailed reporting
            if (controller.consecutive_samples % 100 == 0) {
                SerialWrite("[DX] Freq: ");
                SerialWriteDec(controller.current_freq);
                SerialWrite("Hz | Load: ");
                SerialWriteDec(load_percentage);
                SerialWrite("% | CS: ");
                SerialWriteDec(cs_rate >> FXP_SHIFT);
                SerialWrite(" | Mode: ");
                SerialWriteDec(controller.adaptive_mode);
                SerialWrite(" | Score: ");
                SerialWriteDec(controller.performance_score);
                SerialWrite("\n");
            }

            last_sample_time = current_time;
            last_context_switches = context_switches;
        }

        // Shorter yield for better responsiveness
        for (volatile int i = 0; i < 30000; i++); // Reduced from standard Yield()
    }
}

static int SecureTokenUpdate(Process* proc, uint8_t new_flags) {
    if (!proc || proc->pid == 0) return 0;

    // Only allow self-modification or system process modification
    Process* caller = GetCurrentProcess();
    if (caller->pid != proc->pid && caller->privilege_level != PROC_PRIV_SYSTEM) {
        return 0; // Unauthorized
    }

    // Create new token with updated flags
    SecurityToken new_token = proc->token;
    new_token.flags |= new_flags;
    new_token.checksum = 0; // Clear for recalculation
    new_token.checksum = CalculateSecureChecksum(&new_token, proc->pid);

    // Atomic update
    proc->token = new_token;
    return 1;
}



void Astra(void) {
    PrintKernelSuccess("[AS] Astra initializing...\n");
    Process* current = GetCurrentProcess();

    // register
    security_manager_pid = current->pid;

    // Create system tracer with enhanced security
    CreateSecureProcess(DynamoX, PROC_PRIV_SYSTEM, PROC_FLAG_CORE);

    PrintKernelSuccess("[AS] Astra active.\n");

    uint64_t last_check = 0;

    uint64_t last_integrity_scan = 0;
    uint64_t last_behavior_analysis = 0;
    uint64_t last_memory_scan = 0;
    uint32_t threat_level = 0;
    uint32_t suspicious_activity_count = 0;

    // Dynamic intensity control
    uint32_t base_scan_interval = 100;
    uint32_t current_scan_interval = base_scan_interval;

    while (1) {
        // Adaptive intensity based on system load
        uint32_t system_load = MLFQscheduler.total_processes;
        if (system_load > 5) {
            current_scan_interval = base_scan_interval * 3; // Much less intensive when busy
        } else if (system_load < 3) {
            current_scan_interval = base_scan_interval; // Normal when idle
        } else {
            current_scan_interval = base_scan_interval * 2; // Moderate when normal
        }

        if (current->state == PROC_DYING || current->state == PROC_ZOMBIE) {
            PrintKernelError("[AS] CRITICAL: AS compromised - emergency restart\n");
            // Could trigger system recovery here instead of dying
            PANIC("AS terminated - security system failure");
        }

        const uint64_t current_tick = GetSystemTicks();

        if (current_tick - last_behavior_analysis >= 25) { // Run this check often
            last_behavior_analysis = current_tick;
            uint64_t check_bitmap = active_process_bitmap;
            int proc_scanned = 0;

            while (check_bitmap && proc_scanned < 8) { // Scan a few processes each time
                const int slot = FastFFS(check_bitmap);
                check_bitmap &= ~(1ULL << slot);
                proc_scanned++;

                const Process* proc = &processes[slot];

                // THE CRITICAL CHECK: Is this process running as system without authorization?
                if (proc->privilege_level == PROC_PRIV_SYSTEM &&
                    !(proc->token.flags & (PROC_FLAG_SUPERVISOR | PROC_FLAG_CRITICAL))) {

                    // This process has elevated privileges but is not a trusted supervisor
                    // or a critical process. This is a massive red flag.
                    PrintKernelError("[AS] THREAT: Illicit system process detected! PID: ");
                    PrintKernelInt(proc->pid);
                    PrintKernelError("\n");

                    // Immediately terminate with extreme prejudice.
                    ASTerminate(proc->pid, "Unauthorized privilege escalation");
                    threat_level += 20; // Escalate threat level immediately
                    }
            }
        }

        // 1. Token integrity verification
        if (current_tick - last_integrity_scan >= 50) {
            last_integrity_scan = current_tick;
            uint64_t active_bitmap = active_process_bitmap;
            int scanned = 0;

            // FIX: Increased the number of processes scanned per cycle from 3 to 16
            // to make detection much more effective and timely.
            while (active_bitmap && scanned < 16) {
                const int slot = FastFFS(active_bitmap);
                active_bitmap &= ~(1ULL << slot);
                scanned++;

                const Process* proc = &processes[slot];
                if (proc->state == PROC_READY || proc->state == PROC_RUNNING) {
                    if (proc->pid != security_manager_pid && // Don't check AS itself
                            UNLIKELY(!ValidateToken(&proc->token, proc->pid))) {
                        PrintKernelError("[AS] CRITICAL: Token corruption PID ");
                        PrintKernelInt(proc->pid);
                        PrintKernelError("\n");
                        threat_level += 10;
                        SecurityViolationHandler(proc->pid, "Token corruption");
                    }
                }
            }
        }

        // 3. Memory integrity checks
        if (current_tick - last_memory_scan >= 300) {
            last_memory_scan = current_tick;

            if (MLFQscheduler.current_running >= MAX_PROCESSES) {
                PrintKernelError("[AS] CRITICAL: Scheduler corruption detected\n");
                threat_level += 30;
                PANIC("AS: Critical scheduler corruption - system compromised");
            }

            uint32_t actual_count = __builtin_popcountll(active_process_bitmap);
            if (actual_count != process_count) {
                PrintKernelError("[AS] CRITICAL: Process count corruption\n");
                threat_level += 10;
                suspicious_activity_count++;
            }
        }

        // 4. Bitmap check
        static uint64_t last_sched_scan = 0;

        if (current_tick - last_sched_scan >= SCHED_CONSISTENCY_INTERVAL) {
            last_sched_scan = current_tick;

            uint32_t popcount_processes = __builtin_popcountll(active_process_bitmap);
            if (UNLIKELY(popcount_processes != process_count)) {
                PrintKernelError("[AS] CRITICAL: Process count/bitmap mismatch! System may be unstable.\n");
                // This is a serious issue, but maybe not panic-worthy immediately.
                // Let's increase the threat level aggressively.
                threat_level += 20;
            }
        }

        // Aggressive threat management
        if (threat_level > 75) {
            PANIC("AS-CRITICAL: High threat level indicates unrecoverable system corruption.");
        }

        if (threat_level > 40) {
            // DEFCON 2: Aggressive containment.
            // A serious threat was detected. Terminate all non-critical, non-immune processes.
            PrintKernelError("[AS] DEFCON 2: High threat detected. Initiating selective lockdown.\n");
            for (int i = 1; i < MAX_PROCESSES; i++) {
                Process* p = &processes[i];
                if (p->pid != 0 && p->pid != security_manager_pid &&
                    p->state != PROC_TERMINATED &&
                    !(p->token.flags & (PROC_FLAG_CRITICAL | PROC_FLAG_IMMUNE)))
                {
                    ASTerminate(p->pid, "System-wide security lockdown");
                }
            }
            // After such drastic action, reduce the threat level but not to zero.
            threat_level = 20;
        }

        // Decay the threat level slowly
        if (current_tick % 200 == 0 && threat_level > 0) {
            threat_level--;
        }

        CleanupTerminatedProcesses();
        Yield();
    }
}

int ProcessInit(void) {
    FastMemset(processes, 0, sizeof(Process) * MAX_PROCESSES);

    // Initialize scheduler first to get a valid tick counter
    InitScheduler();

    // Initialize idle process
    Process* idle_proc = &processes[0];
    idle_proc->pid = 0;
    idle_proc->state = PROC_RUNNING;
    idle_proc->privilege_level = PROC_PRIV_SYSTEM;
    idle_proc->scheduler_node = NULL;
    idle_proc->creation_time = GetSystemTicks();

    // Securely initialize the token for the Idle Process
    SecurityToken* token = &idle_proc->token;
    token->magic = SECURITY_MAGIC;
    token->creator_pid = 0;
    token->privilege = PROC_PRIV_SYSTEM;
    token->flags = PROC_FLAG_CORE;
    token->creation_tick = idle_proc->creation_time;
    token->checksum = 0;
    token->checksum = CalculateSecureChecksum(token, 0);

    process_count = 1;
    active_process_bitmap |= 1ULL;

    // Create AS internally during init - no external access
    PrintKernel("[SYSTEM] Creating AS (Astra)...\n");
    uint32_t AS_pid = CreateSecureProcess(Astra, PROC_PRIV_SYSTEM, PROC_FLAG_CORE);
    if (!AS_pid) {
        PANIC("CRITICAL: Failed to create AS - system compromised");
    }
    PrintKernelSuccess("[SYSTEM] AS created with PID: ");
    PrintKernelInt(AS_pid);
    PrintKernel("\n");


    // Create shell process
    PrintKernel("[SYSTEM] Creating shell process...\n");
    uint32_t shell_pid = CreateSecureProcess(ShellProcess, PROC_PRIV_SYSTEM, PROC_FLAG_CORE);
    if (!shell_pid) {
        PANIC("CRITICAL: Failed to create shell process");
    }
    PrintKernelSuccess("[SYSTEM] Shell created with PID: ");
    PrintKernelInt(shell_pid);
    PrintKernel("\n");

    return 0;
}

void DumpPerformanceStats(void) {
    PrintKernel("[PERF] Context switches: ");
    PrintKernelInt((uint32_t)context_switches);
    PrintKernel("\n[PERF] Scheduler calls: ");
    PrintKernelInt((uint32_t)scheduler_calls);
    PrintKernel("\n[PERF] Security violations: ");
    PrintKernelInt(security_violation_count);
    PrintKernel("\n[PERF] Active processes: ");
    PrintKernelInt(__builtin_popcountll(active_process_bitmap));
    PrintKernel("\n[PERF] Avg context switch overhead: ");
    PrintKernelInt(MLFQscheduler.context_switch_overhead);
    PrintKernel(" ticks\n[PERF] System load: ");
    PrintKernelInt(MLFQscheduler.total_processes);
    PrintKernel(" processes\n");
    
    // Show per-priority statistics
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        if (MLFQscheduler.queues[i].count > 0) {
            PrintKernel("[PERF] Priority ");
            PrintKernelInt(i);
            PrintKernel(": ");
            PrintKernelInt(MLFQscheduler.queues[i].count);
            PrintKernel(" procs, avg burst: ");
            PrintKernelInt(MLFQscheduler.queues[i].avg_cpu_burst);
            PrintKernel("\n");
        }
    }
}

static const char* GetStateString(ProcessState state) {
    switch (state) {
        case PROC_TERMINATED: return "TERMINATED";
        case PROC_READY:      return "READY     ";
        case PROC_RUNNING:    return "RUNNING   ";
        case PROC_BLOCKED:    return "BLOCKED   ";
        case PROC_ZOMBIE:     return "ZOMBIE    ";
        case PROC_DYING:      return "DYING     ";
        default:              return "UNKNOWN   ";
    }
}

void ListProcesses(void) {
    PrintKernel("\n--- Enhanced Process List ---\n");
    PrintKernel("PID\tState     \tPrio\tCPU%\tI/O\tPreempt\n");
    PrintKernel("-----------------------------------------------\n");
    
    uint64_t total_cpu_time = 1; // Avoid division by zero
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (i == 0 || processes[i].pid != 0) {
            total_cpu_time += processes[i].cpu_time_accumulated;
        }
    }
    
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (i == 0 || processes[i].pid != 0) {
            const Process* p = &processes[i];
            uint32_t cpu_percent = (uint32_t)((p->cpu_time_accumulated * 100) / total_cpu_time);

            PrintKernelInt(p->pid);
            PrintKernel("\t");
            PrintKernel(GetStateString(p->state));
            PrintKernel("\t");
            PrintKernelInt(p->priority);
            PrintKernel("\t");
            PrintKernelInt(cpu_percent);
            PrintKernel("%\t");
            PrintKernelInt(p->io_operations);
            PrintKernel("\t");
            PrintKernelInt(p->preemption_count);
            PrintKernel("\n");
        }
    }
    PrintKernel("-----------------------------------------------\n");
    PrintKernel("Total CPU time: ");
    PrintKernelInt((uint32_t)total_cpu_time);
    PrintKernel(" ticks\n");
}

void DumpSchedulerState(void) {
    PrintKernel("[SCHED] PIT frequency: ");
    PrintKernelInt(PIT_FREQUENCY_HZ);
    PrintKernel("\n");
    PrintKernel("[SCHED] Current: ");
    PrintKernelInt(MLFQscheduler.current_running);
    PrintKernel(" Quantum: ");
    PrintKernelInt(MLFQscheduler.quantum_remaining);
    PrintKernel(" Load: ");
    PrintKernelInt(MLFQscheduler.total_processes);
    PrintKernel("\n[SCHED] Active: 0x");
    PrintKernelHex(MLFQscheduler.active_bitmap);
    PrintKernel(" RT: 0x");
    PrintKernelHex(MLFQscheduler.rt_bitmap);
    PrintKernel(" Overhead: ");
    PrintKernelInt(MLFQscheduler.context_switch_overhead);
    PrintKernel("\n");

    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        if (MLFQscheduler.queues[i].count > 0) {
            PrintKernel("  L");
            PrintKernelInt(i);
            PrintKernel(i < RT_PRIORITY_THRESHOLD ? "(RT)" : "(RG)");
            PrintKernel(": ");
            PrintKernelInt(MLFQscheduler.queues[i].count);
            PrintKernel(" procs, Q:");
            PrintKernelInt(MLFQscheduler.queues[i].quantum);
            PrintKernel(" AvgBurst:");
            PrintKernelInt(MLFQscheduler.queues[i].avg_cpu_burst);
            PrintKernel("\n");
        }
    }
}

// Get detailed process scheduling information
void GetProcessStats(uint32_t pid, uint32_t* cpu_time, uint32_t* io_ops, uint32_t* preemptions) {
    Process* proc = GetProcessByPid(pid);
    if (!proc) {
        if (cpu_time) *cpu_time = 0;
        if (io_ops) *io_ops = 0;
        if (preemptions) *preemptions = 0;
        return;
    }
    
    if (cpu_time) *cpu_time = (uint32_t)proc->cpu_time_accumulated;
    if (io_ops) *io_ops = proc->io_operations;
    if (preemptions) *preemptions = proc->preemption_count;
}

// Force priority boost for a specific process (for testing/debugging)
void BoostProcessPriority(uint32_t pid) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    
    Process* proc = GetProcessByPid(pid);
    if (!proc || proc->state == PROC_TERMINATED) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        return;
    }
    
    // Remove from current queue if scheduled
    if (proc->scheduler_node) {
        RemoveFromScheduler(proc - processes);
    }
    
    // Boost to highest non-RT priority for user processes
    if (proc->privilege_level == PROC_PRIV_SYSTEM) {
        proc->priority = 0;
    } else {
        proc->priority = RT_PRIORITY_THRESHOLD;
    }
    
    // Re-add to scheduler if ready
    if (proc->state == PROC_READY) {
        AddToScheduler(proc - processes);
    }
    
    SpinUnlockIrqRestore(&scheduler_lock, flags);
}