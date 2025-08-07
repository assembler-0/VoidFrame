#include "Process.h"
#include "Atomics.h"
#include "Console.h"
#include "Cpu.h"
#include "Io.h"
#include "Ipc.h"
#include "MemOps.h"
#include "Memory.h"
#include "Panic.h"
#include "Spinlock.h"

#define offsetof(type, member) ((uint64_t)&(((type*)0)->member))

// Security flags
#define PROC_FLAG_IMMUNE        (1U << 0)
#define PROC_FLAG_CRITICAL      (1U << 1)
#define PROC_FLAG_VALIDATED     (1U << 2)
#define PROC_FLAG_SUPERVISOR    (1U << 3)

// Performance optimizations
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)
#define CACHE_LINE_SIZE         64
#define ALIGNED_CACHE           __attribute__((aligned(CACHE_LINE_SIZE)))

static const uint64_t SECURITY_MAGIC = 0x5EC0DE4D41474943ULL;
static const uint64_t SECURITY_SALT = 0xDEADBEEFCAFEBABEULL;
static const uint32_t MAX_SECURITY_VIOLATIONS = 5;

static Process processes[MAX_PROCESSES] ALIGNED_CACHE;
static volatile uint32_t next_pid = 1;
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
        PANIC("Termination queu1e overflow");
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

    if (UNLIKELY(slot >= MAX_PROCESSES)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        return;
    }

    // Enhanced security checks
    if (LIKELY(reason != TERM_SECURITY)) {
        // Cross-process termination security
        if (caller->pid != proc->pid) {
            // Only system processes can terminate other processes
            if (UNLIKELY(caller->privilege_level != PROC_PRIV_SYSTEM)) {
                SpinUnlockIrqRestore(&scheduler_lock, flags);
                TerminateProcess(caller->pid, TERM_SECURITY, 0);
                return;
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

    AddToTerminationQueueAtomic(slot);
    proc->state = PROC_ZOMBIE;

    // Self-termination handling
    if (UNLIKELY(pid == caller->pid)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        __asm__ __volatile__("cli; hlt" ::: "memory");
    }
    SpinUnlockIrqRestore(&scheduler_lock, flags);
}


static void SecurityViolationHandler(uint32_t violator_pid, const char* reason) {
    AtomicInc(&security_violation_count);

    PrintKernelError("[SYSTEM] Violation by PID ");
    PrintKernelInt(violator_pid);
    PrintKernelError(": ");
    PrintKernelError(reason);
    PrintKernelError("\n");

    if (UNLIKELY(security_violation_count > MAX_SECURITY_VIOLATIONS)) {
        PANIC("Too many security violations - system compromised");
    }

    // Immediate termination with security flag
    TerminateProcess(violator_pid, TERM_SECURITY, 1);
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

    // Initialize with exponential time quantum decay
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        MLFQscheduler.queues[i].quantum = QUANTUM_BASE >> i; // Exponential decay
        MLFQscheduler.queues[i].head = NULL;
        MLFQscheduler.queues[i].tail = NULL;
        MLFQscheduler.queues[i].count = 0;
    }

    MLFQscheduler.current_running = 0;
    MLFQscheduler.quantum_remaining = 0;
    MLFQscheduler.active_bitmap = 0;
    MLFQscheduler.last_boost_tick = 0;
    MLFQscheduler.tick_counter = 1; // Start at 1 for security
}

// Add process to scheduler
void AddToScheduler(uint32_t slot) {
    if (slot == 0) return;  // Don't schedule idle process

    Process* proc = &processes[slot];
    if (proc->state != PROC_READY) return;

    // Determine initial priority based on privilege level
    uint32_t priority = 0;
    if (proc->privilege_level == PROC_PRIV_SYSTEM) {
        priority = 0;  // Highest priority for system processes
    } else {
        priority = 1;  // Start user processes at level 1
    }

    // Clamp priority
    if (priority >= MAX_PRIORITY_LEVELS) {
        priority = MAX_PRIORITY_LEVELS - 1;
    }

    proc->priority = priority;
    EnQueue(&MLFQscheduler.queues[priority], slot);
    MLFQscheduler.active_bitmap |= (1U << priority);
}

// Remove process from scheduler
void RemoveFromScheduler(uint32_t slot) {
    if (slot == 0 || slot >= MAX_PROCESSES) return;

    SchedulerNode* node = processes[slot].scheduler_node;
    if (!node) return;  // Process not in any queue

    // Find which queue this node belongs to by checking the process priority
    uint32_t priority = processes[slot].priority;
    if (priority >= MAX_PRIORITY_LEVELS) return;

    PriorityQueue* q = &MLFQscheduler.queues[priority];

    // Remove node from doubly-linked list
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        // This was the head
        q->head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        // This was the tail
        q->tail = node->prev;
    }

    q->count--;

    // Update bitmap if queue became empty
    if (q->count == 0) {
        MLFQscheduler.active_bitmap &= ~(1U << priority);
    }

    // Clear process reference and free node
    processes[slot].scheduler_node = NULL;
    FreeSchedulerNode(node);
}

static inline int FindHighestPriorityQueue(void) {
    if (MLFQscheduler.active_bitmap == 0) return -1;

    // Find first set bit (lowest index = highest priority)
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        if (MLFQscheduler.active_bitmap & (1U << i)) {
            return i;
        }
    }
    return -1;
}

// Boost all processes to prevent starvation
static void BoostAllProcesses(void) {
    // Move all processes to highest priority queue
    for (int level = 1; level < MAX_PRIORITY_LEVELS; level++) {
        PriorityQueue* src = &MLFQscheduler.queues[level];
        PriorityQueue* dst = &MLFQscheduler.queues[0];

        // Move all nodes from src to dst
        while (src->head) {
            SchedulerNode* node = src->head;
            uint32_t slot = node->slot;

            // Remove from source queue
            src->head = node->next;
            if (src->head) {
                src->head->prev = NULL;
            } else {
                src->tail = NULL;
            }
            src->count--;

            // Update process priority
            processes[slot].priority = 0;

            // Add to destination queue
            node->next = NULL;
            node->prev = dst->tail;

            if (dst->tail) {
                dst->tail->next = node;
                dst->tail = node;
            } else {
                dst->head = dst->tail = node;
            }
            dst->count++;
        }

        MLFQscheduler.active_bitmap &= ~(1U << level);
    }

    if (MLFQscheduler.queues[0].count > 0) {
        MLFQscheduler.active_bitmap |= 1U;
    }
}

// Main scheduler - called from timer interrupt
// Main scheduler - called from timer interrupt
void FastSchedule(struct Registers* regs) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);

    AtomicInc(&scheduler_calls);
    AtomicInc(&MLFQscheduler.tick_counter);

    // Periodic starvation prevention
    if (UNLIKELY(MLFQscheduler.tick_counter - MLFQscheduler.last_boost_tick >= BOOST_INTERVAL)) {
        BoostAllProcesses();
        MLFQscheduler.last_boost_tick = MLFQscheduler.tick_counter;
    }

    uint32_t old_slot = MLFQscheduler.current_running;
    Process* old_proc = &processes[old_slot];

    // Handle currently running process
    if (LIKELY(old_slot != 0)) {
        ProcessState state = old_proc->state;

        // Fast path for dead processes
        if (UNLIKELY(state == PROC_DYING || state == PROC_ZOMBIE || state == PROC_TERMINATED)) {
            // Process is dead, don't save context
            goto select_next;
        }

        // Save context efficiently
        FastMemcpy(&old_proc->context, regs, sizeof(struct Registers));

        // Quantum management
        if (LIKELY(MLFQscheduler.quantum_remaining > 0)) {
            MLFQscheduler.quantum_remaining--;
        }

        // Preemption check
        int highest_priority = FindHighestPriorityQueue();

        // Continue if quantum remains and no higher priority process
        if (LIKELY(MLFQscheduler.quantum_remaining > 0 &&
                  (highest_priority == -1 || highest_priority > (int)old_proc->priority))) {
            SpinUnlockIrqRestore(&scheduler_lock, flags);
            return; // No context switch needed
        }

        // Context switch needed - prepare old process
        old_proc->state = PROC_READY;
        ready_process_bitmap |= (1ULL << old_slot);

        // Adaptive priority adjustment
        if (LIKELY(old_proc->priority < MAX_PRIORITY_LEVELS - 1)) {
            old_proc->priority++;
        }

        AddToScheduler(old_slot);
    }

select_next:;
    // Find next process to run
    int next_priority = FindHighestPriorityQueue();
    uint32_t next_slot;

    if (UNLIKELY(next_priority == -1)) {
        next_slot = 0; // Run idle process
    } else {
        next_slot = DeQueue(&MLFQscheduler.queues[next_priority]);

        // Validate next process
        if (UNLIKELY(next_slot >= MAX_PROCESSES ||
                    processes[next_slot].state != PROC_READY)) {
            next_slot = 0; // Fall back to idle
        }
    }

    // Context switch
    MLFQscheduler.current_running = next_slot;
    current_process = next_slot;

    if (LIKELY(next_slot != 0)) {
        Process* new_proc = &processes[next_slot];
        new_proc->state = PROC_RUNNING;
        ready_process_bitmap &= ~(1ULL << next_slot);

        // Set quantum for new process
        MLFQscheduler.quantum_remaining = MLFQscheduler.queues[new_proc->priority].quantum;

        // Restore context
        FastMemcpy(regs, &new_proc->context, sizeof(struct Registers));
        AtomicInc(&context_switches);
    } else {
        MLFQscheduler.quantum_remaining = 0;
    }
    SpinUnlockIrqRestore(&scheduler_lock, flags);
}

// Called when process blocks (I/O, IPC, etc.)
void ProcessBlocked(uint32_t slot) {
    if (slot == MLFQscheduler.current_running) {
        // Current process blocked, trigger immediate reschedule
        MLFQscheduler.quantum_remaining = 0;
        RequestSchedule();
    }

    // When process unblocks, it goes to higher priority (I/O bound processes get priority)
    Process* proc = &processes[slot];
    if (proc->state == PROC_READY && proc->priority > 0) {
        proc->priority--;  // Boost priority
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
    Process* current = GetCurrentProcess();
    if (current) {
        current->state = PROC_BLOCKED; // Mark as blocked for scheduler to boost
    }
    RequestSchedule();
    __asm__ __volatile__("hlt");
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
    idle_proc->creation_time = GetSystemTicks(); // Use the official tick getter

    // Securely initialize the token for the Idle Process
    SecurityToken* token = &idle_proc->token;
    token->magic = SECURITY_MAGIC;
    token->creator_pid = 0; // Itself
    token->privilege = PROC_PRIV_SYSTEM;
    token->flags = PROC_FLAG_IMMUNE | PROC_FLAG_CRITICAL; // The idle process is both
    token->creation_tick = idle_proc->creation_time;

    // Calculate the one, true checksum
    token->checksum = 0; // Must be zero for calculation
    token->checksum = CalculateSecureChecksum(token, 0);

    process_count = 1;
    active_process_bitmap |= 1ULL; // Mark slot 0 as active

    return 0;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER);
}

void ProcessExitStub() {
    Process* current = GetCurrentProcess();

    PrintKernelWarning("[SYSTEM] Process PID ");
    PrintKernelInt(current->pid);
    PrintKernelWarning(" exited normally\n");

    // Use the safe termination function
    TerminateProcess(current->pid, TERM_NORMAL, 0);
    // Should not reach here, but just in case
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    if (UNLIKELY(!entry_point)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: NULL entry point");
    }

    Process* creator = GetCurrentProcess();

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

    uint32_t new_pid = __sync_fetch_and_add(&next_pid, 1);

    // Clear slot securely
    FastMemset(&processes[slot], 0, sizeof(Process));

    // Allocate aligned stack
    void* stack = AllocPage();
    if (UNLIKELY(!stack)) {
        FreeSlotFast(slot);
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("CreateSecureProcess: Failed to allocate stack");
    }

    // Initialize process with enhanced security
    processes[slot].pid = new_pid;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].privilege_level = privilege;
    processes[slot].priority = (privilege == PROC_PRIV_SYSTEM) ? 0 : 1;
    processes[slot].is_user_mode = (privilege != PROC_PRIV_SYSTEM);
    processes[slot].scheduler_node = NULL;
    processes[slot].creation_time = GetSystemTicks();

    // Enhanced token initialization
    SecurityToken* token = &processes[slot].token;
    token->magic = SECURITY_MAGIC;
    token->creator_pid = creator->pid;
    token->privilege = privilege;
    token->flags = 0;
    token->creation_tick = GetSystemTicks();
    token->checksum = CalculateSecureChecksum(token, new_pid);

    // Set up secure initial context
    uint64_t rsp = (uint64_t)stack + STACK_SIZE;
    rsp &= ~0xF; // 16-byte alignment

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

void ScheduleFromInterrupt(struct Registers* regs) {
    FastSchedule(regs);
}
void CleanupTerminatedProcesses(void) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    // Process a limited number per call to avoid long interrupt delays
    int cleanup_count = 0;
    const int MAX_CLEANUP_PER_CALL = 3;

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
            FreePage(proc->stack);
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

void RegisterSecurityManager(uint32_t pid) {
    security_manager_pid = pid;
}

void SystemTracer(void) {
    PrintKernelSuccess("[SYSTEM] SystemTracer() has started. Scanning...\n");
    while (1) {
        CleanupTerminatedProcesses();
        Yield();
    }
}

void SecureKernelIntegritySubsystem(void) {
    PrintKernelSuccess("[SYSTEM] SecureKernelIntegritySubsystem() initializing...\n");
    Process* current = GetCurrentProcess();

    // Make SKIS immune and critical
    current->token.flags |= (PROC_FLAG_IMMUNE | PROC_FLAG_CRITICAL | PROC_FLAG_SUPERVISOR);

    // FIX: The checksum field must be zeroed before recalculating the hash.
    current->token.checksum = 0;
    current->token.checksum = CalculateSecureChecksum(&current->token, current->pid);

    RegisterSecurityManager(current->pid);

    // Create system tracer with enhanced security
    uint32_t tracer_pid = CreateSecureProcess(SystemTracer, PROC_PRIV_SYSTEM);
    if (tracer_pid) {
        Process* tracer = GetProcessByPid(tracer_pid);
        if (tracer) {
            tracer->token.flags |= (PROC_FLAG_IMMUNE | PROC_FLAG_CRITICAL);
            // Also fix it here for the tracer process!
            tracer->token.checksum = 0;
            tracer->token.checksum = CalculateSecureChecksum(&tracer->token, tracer_pid);
        }
    }

    PrintKernelSuccess("[SYSTEM] SecureKernelIntegritySubsystem() active\n");

    uint64_t last_check = 0;

    while (1) {
         const uint64_t current_tick = GetSystemTicks();
        // Throttled security checking to reduce overhead
        if (current_tick - last_check >= 100) { // Check every 100 ticks
            last_check = current_tick;

            // Fast security scan using bitmaps
            uint64_t active_bitmap = active_process_bitmap;

            while (active_bitmap) {
                const int slot = FastFFS(active_bitmap);
                active_bitmap &= ~(1ULL << slot);

                const Process * proc = &processes[slot];

                if (proc->state == PROC_READY || proc->state == PROC_RUNNING) {
                    if (UNLIKELY(!ValidateToken(&proc->token, proc->pid))) {
                        PrintKernelError("[SYSTEM] Token corruption detected for PID ");
                        PrintKernelInt(proc->pid);
                        PrintKernelError("\n");

                        SecurityViolationHandler(proc->pid, "Token corruption");
                    }
                }
            }
        }
        // Cleanup and yield
        CleanupTerminatedProcesses();
        Yield();
    }
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
    PrintKernel("\n");
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
    PrintKernel("--- Process List ---\n");
    PrintKernel("PID\tState     \tPriv  \tImmune\n");
    PrintKernel("-------------------------------------\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        // --- CHANGE HERE: The condition now correctly includes the Idle Process (i == 0) ---
        if (i == 0 || processes[i].pid != 0) {
            const Process * p = &processes[i];

            PrintKernelInt(p->pid);
            PrintKernel("\t");
            PrintKernel(GetStateString(p->state));
            PrintKernel("\t");
            PrintKernel(p->privilege_level == PROC_PRIV_SYSTEM ? "SYSTEM" : "USER  ");
            PrintKernel("\t");
            PrintKernel(p->token.flags & PROC_FLAG_IMMUNE ? "YES" : "NO");
            PrintKernel("\n");
        }
    }
    PrintKernel("-------------------------------------\n");
}

void DumpSchedulerState(void) {
    PrintKernel("[SCHED] Current: ");
    PrintKernelInt(MLFQscheduler.current_running);
    PrintKernel(" Quantum: ");
    PrintKernelInt(MLFQscheduler.quantum_remaining);
    PrintKernel(" Bitmap: ");
    PrintKernelInt(MLFQscheduler.active_bitmap);
    PrintKernel("\n");

    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        if (MLFQscheduler.queues[i].count > 0) {
            PrintKernel("  Priority ");
            PrintKernelInt(i);
            PrintKernel(": ");
            PrintKernelInt(MLFQscheduler.queues[i].count);
            PrintKernel(" processes, quantum: ");
            PrintKernelInt(MLFQscheduler.queues[i].quantum);
            PrintKernel("\n");
        }
    }
}