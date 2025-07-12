#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
#include "Panic.h"
#include "../Drivers/Cpu.h"
#include "../Memory/MemOps.h"
#include "../Core/Ipc.h"
#define offsetof(type, member) ((uint64_t)&(((type*)0)->member))

static Process processes[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_process = 0;
static uint32_t process_count = 0;
static volatile int need_schedule = 0;
static uint32_t security_manager_pid = 0;
static const uint64_t security_magic = 0x5EC0DE4D41474943ULL;

extern void SwitchContext(ProcessContext * old, ProcessContext * new);

static uint32_t last_scheduled_slot = 0;  // For round-robin optimization
static uint32_t active_process_bitmap = 0;  // Bitmap for fast slot tracking (up to 32 processes)

static Scheduler MLFQscheduler;
static SchedulerNode scheduler_node_pool[MAX_PROCESSES];
static uint32_t scheduler_node_pool_bitmap[(MAX_PROCESSES + 31) / 32];

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
// Fast slot allocation using bitmap
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
    InitSchedulerNodePool();  // Initialize the node pool

    // Initialize priority queues with different time quantums
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        MLFQscheduler.queues[i].quantum = QUANTUM_BASE / (1 << i);
        MLFQscheduler.queues[i].head = NULL;
        MLFQscheduler.queues[i].tail = NULL;
        MLFQscheduler.queues[i].count = 0;
    }

    MLFQscheduler.current_running = 0;
    MLFQscheduler.quantum_remaining = 0;
    MLFQscheduler.active_bitmap = 0;
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
void FastSchedule(struct Registers* regs) {
    MLFQscheduler.tick_counter++;

    // Boost all processes periodically to prevent starvation
    if (MLFQscheduler.tick_counter - MLFQscheduler.last_boost_tick >= BOOST_INTERVAL) {
        BoostAllProcesses();
        MLFQscheduler.last_boost_tick = MLFQscheduler.tick_counter;
    }

    Process* current = &processes[MLFQscheduler.current_running];

    // Handle current process
    if (MLFQscheduler.current_running != 0) {  // Not idle
        // Save context
        FastMemcpy(&current->context, regs, sizeof(struct Registers));

        // Update process state
        if (current->state == PROC_RUNNING) {
            current->state = PROC_READY;

            // Check if quantum expired
            if (MLFQscheduler.quantum_remaining > 0) {
                MLFQscheduler.quantum_remaining--;

                // If quantum not expired and no higher priority processes, keep running
                int highest_priority = FindHighestPriorityQueue();
                if (MLFQscheduler.quantum_remaining > 0 &&
                    (highest_priority == -1 || highest_priority >= (int)current->priority)) {
                    current->state = PROC_RUNNING;
                    return;  // Keep current process running
                }
            }

            // Quantum expired or higher priority process available
            // Demote to lower priority (unless already at lowest)
            if (current->priority < MAX_PRIORITY_LEVELS - 1) {
                current->priority++;
            }

            // Add back to appropriate queue
            AddToScheduler(MLFQscheduler.current_running);
        }
    }

    // Find next process to run
    int next_priority = FindHighestPriorityQueue();
    if (next_priority == -1) {
        // No processes ready, run idle
        MLFQscheduler.current_running = 0;
        MLFQscheduler.quantum_remaining = 0;
    } else {
        // Get next process from highest priority queue
        uint32_t next_slot = DeQueue(&MLFQscheduler.queues[next_priority]);
        if (MLFQscheduler.queues[next_priority].count == 0) {
            MLFQscheduler.active_bitmap &= ~(1U << next_priority);
        }

        MLFQscheduler.current_running = next_slot;
        MLFQscheduler.quantum_remaining = MLFQscheduler.queues[next_priority].quantum;
    }


    processes[MLFQscheduler.current_running].state = PROC_RUNNING;
    FastMemcpy(regs, &processes[MLFQscheduler.current_running].context, sizeof(struct Registers));
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

// Simple interface for your existing code


static int FindFreeSlot(void) {
    // Skip slot 0 (idle process)
    for (int i = 1; i < MAX_PROCESSES && i < 32; i++) {
        if (!(active_process_bitmap & (1U << i))) {
            // Double-check that slot is actually free
            if (processes[i].state == PROC_TERMINATED ||
                (processes[i].pid == 0 && processes[i].state == 0)) {
                active_process_bitmap |= (1U << i);  // Mark as occupied
                return i;
                }
        }
    }
    return -1;  // No free slots
}

// Mark slot as free
static void FreeSlot(int slot) {
    if (slot > 0 && slot < 32) {
        active_process_bitmap &= ~(1U << slot);
    }
}

static uint16_t CalculateChecksum(const SecurityToken* token, uint32_t pid_for_checksum) {
    uint16_t sum = 0;
    const uint8_t* data = (const uint8_t*)token;

    for (uint64_t i = 0; i < offsetof(SecurityToken, checksum); i++) {
        sum += data[i];
    }

    sum += (uint16_t)(pid_for_checksum & 0xFFFF);
    sum += (uint16_t)((pid_for_checksum >> 16) & 0xFFFF);

    return sum;
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

static int ValidateToken(const SecurityToken* token, uint32_t pid_to_check) {
    if (token->magic != security_magic) {
        return 0;
    }

    uint16_t calculated_checksum = CalculateChecksum(token, pid_to_check);
    if (token->checksum != calculated_checksum) {
        return 0;
    }

    return 1;
}

static void init_token(SecurityToken* token, uint32_t creator_pid, uint8_t privilege, uint32_t new_pid) {
    token->magic = security_magic;
    token->creator_pid = creator_pid;
    token->privilege = privilege;
    token->flags = 0;
    token->checksum = 0;
    token->checksum = CalculateChecksum(token, new_pid);
}

int ProcessInit(void) {
    FastMemset(processes, 0, sizeof(Process) * MAX_PROCESSES);

    // Initialize idle process
    processes[0].pid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].privilege_level = PROC_PRIV_SYSTEM;
    processes[0].scheduler_node = NULL;

    InitScheduler();  // This now initializes the node pool too
    process_count = 1;
    return 0;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER);
}

void ProcessExitStub() {
    PrintKernelError("[KERNEL] Process returned from its main function. -FATAL EXECPTION-\n");
    PrintKernelWarning("Terminating process PID: ");
    PrintKernelInt(GetCurrentProcess()->pid);
    PrintKernel("\n");

    GetCurrentProcess()->state = PROC_TERMINATED;
    RequestSchedule();
    while (1) { __asm__ __volatile__("hlt"); }
}

uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege) {
    if (!entry_point) {
        Panic("CreateSecureProcess: NULL entry point");
    }

    Process* creator = GetCurrentProcess();
    // Security check
    if (privilege == PROC_PRIV_SYSTEM) {
        if (creator->pid != 0 && creator->privilege_level != PROC_PRIV_SYSTEM) {
            PrintKernelError("[SYSTEM] Denied: PID ");
            PrintKernelInt(creator->pid);
            PrintKernelError(" attempted to create a system-level process.\n");
            return 0;
        }
    }

    if (process_count >= MAX_PROCESSES) {
        Panic("CreateSecureProcess: Too many processes");
    }

    // Get next available slot
    int slot = FindFreeSlot();
    if (slot == -1) {
        Panic("CreateSecureProcess: No free process slots");
    }

    uint32_t new_pid = next_pid++;

    // Clear the entire slot first
    FastMemset(&processes[slot], 0, sizeof(Process));



    // Allocate stack
    void* stack = AllocPage();
    if (!stack) {
        FreeSlot(slot);  // Free the slot on failure
        Panic("CreateSecureProcess: Failed to allocate stack");
    }

    // Initialize process fields
    processes[slot].pid = new_pid;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].privilege_level = privilege;
    processes[slot].priority = (privilege == PROC_PRIV_SYSTEM) ? 10 : 100;
    processes[slot].is_user_mode = (privilege != PROC_PRIV_SYSTEM);
    processes[slot].weight = (privilege == PROC_PRIV_SYSTEM) ? 100 : 50;
    processes[slot].cpu_time_accumulated = 0;
    processes[slot].dynamic_priority_score = 0;
    processes[slot].scheduler_node = NULL;

    // Initialize IPC queue
    processes[slot].ipc_queue.head = 0;
    processes[slot].ipc_queue.tail = 0;
    processes[slot].ipc_queue.count = 0;

    // Create the token
    init_token(&processes[slot].token, creator->pid, privilege, new_pid);

    // Set up the initial context
    uint64_t rsp = (uint64_t)stack + STACK_SIZE;
    rsp &= ~0xF;

    uint64_t* stack_ptr = (uint64_t*)rsp;
    *(--stack_ptr) = (uint64_t)&ProcessExitStub;

    processes[slot].context.rsp = (uint64_t)stack_ptr;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202;
    processes[slot].context.cs = 0x08;
    processes[slot].context.ss = 0x10;

    process_count++;
    AddToScheduler(slot);
    return new_pid;
}

void ScheduleFromInterrupt(struct Registers* regs) {
    FastSchedule(regs);
}


void CleanupTerminatedProcesses(void) {
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_TERMINATED && processes[i].pid != 0) {
            if (processes[i].stack) {
                FreePage(processes[i].stack);
            }
            FastMemset(&processes[i], 0, sizeof(Process));
            FreeSlot(i);
            process_count--;
        }
    }
}

Process* GetCurrentProcess(void) {
    if (current_process >= MAX_PROCESSES) {
        Panic("GetCurrentProcess: Invalid current process index");
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
        __asm__ __volatile__("hlt");
    }
}

void SecureKernelIntegritySubsystem(void) {
    PrintKernelSuccess("[SYSTEM] MLFQ scheduler initializing...\n");
    PrintKernelSuccess("[SYSTEM] SecureKernelIntegritySubsystem() initializing...\n");
    Process* current = GetCurrentProcess();
    RegisterSecurityManager(current->pid);

    PrintKernelSuccess("[SYSTEM] Creating system service...\n");
    uint32_t service_pid = CreateSecureProcess(SystemTracer, PROC_PRIV_SYSTEM);
    if (service_pid) {
        PrintKernelSuccess("[SYSTEM] System now under SecureKernelIntegritySubsystem() control.\n");
    } else {
        Panic("[SYSTEM] Failed to create system service.\n");
    }
    PrintKernelSuccess("[SYSTEM] SecureKernelIntegritySubsystem() deploying...\n");
    while (1) {
        Yield();
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if ((processes[i].state == PROC_READY || processes[i].state == PROC_RUNNING) && processes[i].pid != 0) {
                if (!ValidateToken(&processes[i].token, processes[i].pid)) {
                    PrintKernel("[SYSTEM] SecureKernelIntegritySubsystem found a corrupt token for PID: ");
                    PrintKernelInt(processes[i].pid);
                    PrintKernel("! Terminating.\n");
                    processes[i].state = PROC_TERMINATED;
                    if (processes[i].stack) {
                        FreePage(processes[i].stack);
                        processes[i].stack = NULL;
                    }
                    process_count--;
                }
            }
        }
    }
}
