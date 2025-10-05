#include "EEVDF.h"
#include "APIC.h"
#include "Atomics.h"
#include "Compositor.h"
#include "KernelHeap.h"
#ifdef VF_CONFIG_USE_CERBERUS
#include "Cerberus.h"
#endif
#include "Console.h"
#include "Format.h"
#include "Io.h"
#include "Ipc.h"
#include "MemOps.h"
#include "Panic.h"
#include "Shell.h"
#include "Spinlock.h"
#include "VFS.h"
#include "VMem.h"
#include "x64.h"

#define offsetof(type, member) ((uint64_t)&(((type*)0)->member))

// Performance optimizations
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)
#define CACHE_LINE_SIZE         64
#define ALIGNED_CACHE           __attribute__((aligned(CACHE_LINE_SIZE)))

// Security constants
static const uint64_t EEVDF_SECURITY_MAGIC = 0x5EC0DE4D41474943ULL;
static const uint64_t EEVDF_SECURITY_SALT = 0xDEADBEEFCAFEBABEULL;
static const uint32_t EEVDF_MAX_SECURITY_VIOLATIONS = EEVDF_SECURITY_VIOLATION_LIMIT;

// Nice-to-weight conversion tables (based on Linux CFS)
const uint32_t eevdf_nice_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};

const uint32_t eevdf_nice_to_wmult[40] = {
 /* -20 */         48388,     59856,     76040,     92818,    118348,
 /* -15 */        147320,    184698,    229616,    287308,    360437,
 /* -10 */        449829,    563644,    704093,    875809,   1099582,
 /*  -5 */       1376151,   1717300,   2157191,   2708050,   3363326,
 /*   0 */       4194304,   5237765,   6557202,   8165337,  10153587,
 /*   5 */      12820798,  15790321,  19976592,  24970740,  31350126,
 /*  10 */      39045157,  49367440,  61356676,  76695844,  95443717,
 /*  15 */     119304647, 148102320, 186737708, 238609294, 286331153,
};

// Global state
static EEVDFProcessControlBlock processes[EEVDF_MAX_PROCESSES] ALIGNED_CACHE;
static volatile uint32_t next_pid = 1;
static uint64_t pid_bitmap[EEVDF_MAX_PROCESSES / 64 + 1] = {0};
static volatile irq_flags_t pid_lock = 0;
static volatile uint32_t current_process = 0;
static volatile uint32_t process_count = 0;
static volatile int need_schedule = 0;
static volatile int scheduler_lock = 0;
rwlock_t process_table_rwlock_eevdf = {0};

// Security subsystem
uint32_t eevdf_security_manager_pid = 0;
static uint32_t security_violation_count = 0;
static uint64_t last_security_check = 0;

// Fast bitmap operations for process slots
static uint64_t active_process_bitmap = 0;
static uint64_t ready_process_bitmap = 0;

// Main scheduler instance
static EEVDFScheduler eevdf_scheduler ALIGNED_CACHE;
static EEVDFRBNode rb_node_pool[EEVDF_MAX_PROCESSES] ALIGNED_CACHE;
static uint32_t rb_node_pool_bitmap[(EEVDF_MAX_PROCESSES + 31) / 32];

// Lockless termination queue
static volatile uint32_t termination_queue[EEVDF_MAX_PROCESSES];
static volatile uint32_t term_queue_head = 0;
static volatile uint32_t term_queue_tail = 0;
static volatile uint32_t term_queue_count = 0;

// Performance counters
static uint64_t context_switches = 0;
static uint64_t scheduler_calls = 0;

extern volatile uint32_t APIC_HZ;
extern volatile uint32_t APICticks;

// =============================================================================
// Utility Functions
// =============================================================================

static inline int FastFFS(const uint64_t value) {
    return __builtin_ctzll(value);
}

static inline int FastCLZ(const uint64_t value) {
    return __builtin_clzll(value);
}

static inline uint64_t GetNS(void) {
    // Convert APIC ticks to nanoseconds
    // Assuming APIC_HZ is in Hz, convert to ns
    return (APICticks * 1000000000ULL) / APIC_HZ;
}

uint64_t EEVDFGetNanoseconds(void) {
    return GetNS();
}

uint64_t EEVDFGetSystemTicks(void) {
    return APICticks;
}

// =============================================================================
// Nice Level Functions  
// =============================================================================

uint32_t EEVDFNiceToWeight(int nice) {
    if (nice < EEVDF_MIN_NICE) nice = EEVDF_MIN_NICE;
    if (nice > EEVDF_MAX_NICE) nice = EEVDF_MAX_NICE;
    return eevdf_nice_to_weight[nice - EEVDF_MIN_NICE];
}

uint32_t EEVDFNiceToWmult(int nice) {
    if (nice < EEVDF_MIN_NICE) nice = EEVDF_MIN_NICE;
    if (nice > EEVDF_MAX_NICE) nice = EEVDF_MAX_NICE;
    return eevdf_nice_to_wmult[nice - EEVDF_MIN_NICE];
}

void EEVDFSetTaskNice(EEVDFProcessControlBlock* p, int nice) {
    if (!p) return;
    
    if (nice < EEVDF_MIN_NICE) nice = EEVDF_MIN_NICE;
    if (nice > EEVDF_MAX_NICE) nice = EEVDF_MAX_NICE;
    
    p->nice = nice;
    p->weight = EEVDFNiceToWeight(nice);
    p->inv_weight = EEVDFNiceToWmult(nice);
}

// =============================================================================
// Virtual Time Calculations
// =============================================================================

uint64_t EEVDFCalcDelta(uint64_t delta_exec, uint32_t weight, uint32_t lw) {
    if (lw == 0) {
        lw = EEVDF_NICE_0_LOAD;
    }
    uint64_t fact = (weight << 16) / lw; // Shifted for precision
    return (delta_exec * fact) >> 16;
}

uint64_t EEVDFCalcSlice(EEVDFRunqueue* rq, EEVDFProcessControlBlock* se) {
    uint32_t nr_running = rq->nr_running;
    if (nr_running == 0) return EEVDF_TIME_SLICE_NS;
    
    uint64_t slice = (EEVDF_TARGET_LATENCY * se->weight) / rq->load_weight;
    
    // Ensure minimum granularity
    if (slice < EEVDF_MIN_GRANULARITY) {
        slice = EEVDF_MIN_GRANULARITY;
    }
    
    // Cap maximum slice
    if (slice > EEVDF_MAX_TIME_SLICE_NS) {
        slice = EEVDF_MAX_TIME_SLICE_NS;
    }
    
    return slice;
}

void EEVDFUpdateCurr(EEVDFRunqueue* rq, EEVDFProcessControlBlock* curr) {
    uint64_t now = GetNS();
    uint64_t delta_exec = now - curr->exec_start;
    
    if (delta_exec == 0) return;
    
    curr->exec_start = now;
    curr->sum_exec_runtime += delta_exec;
    curr->cpu_time_accumulated += delta_exec;
    
    // Update vruntime
    uint64_t delta_fair = EEVDFCalcDelta(delta_exec, EEVDF_NICE_0_LOAD, rq->load_weight);
    curr->vruntime += delta_fair;
    
    // Update runqueue minimum vruntime
    if (rq->rb_leftmost) {
        EEVDFProcessControlBlock* leftmost = &processes[rq->rb_leftmost->slot];
        rq->min_vruntime = leftmost->vruntime;
    } else {
        rq->min_vruntime = curr->vruntime;
    }
}

// =============================================================================
// Red-Black Tree Operations
// =============================================================================

static void EEVDFRBNodeInit(EEVDFRBNode* node, uint32_t slot) {
    node->left = NULL;
    node->right = NULL; 
    node->parent = NULL;
    node->color = 1; // Red
    node->slot = slot;
}

static EEVDFRBNode* EEVDFAllocRBNode(uint32_t slot) {
    for (uint32_t i = 0; i < EEVDF_MAX_PROCESSES; i++) {
        uint32_t word_idx = i / 32;
        uint32_t bit_idx = i % 32;
        
        if (!(rb_node_pool_bitmap[word_idx] & (1U << bit_idx))) {
            rb_node_pool_bitmap[word_idx] |= (1U << bit_idx);
            EEVDFRBNode* node = &rb_node_pool[i];
            EEVDFRBNodeInit(node, slot);
            return node;
        }
    }
    return NULL;
}

static void EEVDFFreeRBNode(EEVDFRBNode* node) {
    if (!node) return;
    
    uint32_t index = node - rb_node_pool;
    if (index >= EEVDF_MAX_PROCESSES) return;
    
    uint32_t word_idx = index / 32;
    uint32_t bit_idx = index % 32;
    
    rb_node_pool_bitmap[word_idx] &= ~(1U << bit_idx);
    node->left = node->right = node->parent = NULL;
    node->color = 0;
    node->slot = 0;
}

// Red-black tree rotation operations
static void EEVDFRBRotateLeft(EEVDFRunqueue* rq, EEVDFRBNode* x) {
    EEVDFRBNode* y = x->right;
    x->right = y->left;
    
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    
    if (!x->parent) {
        rq->rb_root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    
    y->left = x;
    x->parent = y;
}

static void EEVDFRBRotateRight(EEVDFRunqueue* rq, EEVDFRBNode* y) {
    EEVDFRBNode* x = y->left;
    y->left = x->right;
    
    if (x->right) x->right->parent = y;
    x->parent = y->parent;
    
    if (!y->parent) {
        rq->rb_root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    
    x->right = y;
    y->parent = x;
}

// Red-black tree insertion fixup
static void EEVDFRBInsertFixup(EEVDFRunqueue* rq, EEVDFRBNode* z) {
    while (z->parent && z->parent->color == 1) { // parent is red
        if (z->parent == z->parent->parent->left) {
            EEVDFRBNode* y = z->parent->parent->right; // uncle
            if (y && y->color == 1) { // uncle is red
                z->parent->color = 0; // black
                y->color = 0;
                z->parent->parent->color = 1; // red
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    EEVDFRBRotateLeft(rq, z);
                }
                z->parent->color = 0;
                z->parent->parent->color = 1;
                EEVDFRBRotateRight(rq, z->parent->parent);
            }
        } else {
            EEVDFRBNode* y = z->parent->parent->left; // uncle
            if (y && y->color == 1) { // uncle is red
                z->parent->color = 0;
                y->color = 0;
                z->parent->parent->color = 1;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    EEVDFRBRotateRight(rq, z);
                }
                z->parent->color = 0;
                z->parent->parent->color = 1;
                EEVDFRBRotateLeft(rq, z->parent->parent);
            }
        }
    }
    rq->rb_root->color = 0; // root is always black
}

// Insert into red-black tree ordered by vruntime
static void EEVDFRBInsert(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p) {
    // Check if process is already in tree
    if (p->rb_node) {
        PANIC("EEVDFRBInsert: Process already in tree");
    }
    
    EEVDFRBNode* node = EEVDFAllocRBNode(p - processes);
    if (!node) return;
    
    p->rb_node = node;
    
    EEVDFRBNode* parent = NULL;
    EEVDFRBNode** link = &rq->rb_root;
    int leftmost = 1;
    
    // Find insertion point
    while (*link) {
        parent = *link;
        EEVDFProcessControlBlock* entry = &processes[parent->slot];
        
        if (p->vruntime < entry->vruntime) {
            link = &parent->left;
        } else {
            link = &parent->right;
            leftmost = 0;
        }
    }
    
    // Update leftmost pointer
    if (leftmost) {
        rq->rb_leftmost = node;
    }
    
    // Insert node
    node->parent = parent;
    *link = node;
    
    EEVDFRBInsertFixup(rq, node);
}

// Find minimum node (leftmost)
static EEVDFRBNode* EEVDFRBFirst(EEVDFRBNode* root) {
    if (!root) return NULL;
    
    while (root->left) {
        root = root->left;
    }
    return root;
}

// Delete fixup for red-black tree
static void EEVDFRBDeleteFixup(EEVDFRunqueue* rq, EEVDFRBNode* x, EEVDFRBNode* parent) {
    while (x != rq->rb_root && (!x || x->color == 0)) {
        if (x == parent->left) {
            EEVDFRBNode* w = parent->right;
            if (w && w->color == 1) {
                w->color = 0;
                parent->color = 1;
                EEVDFRBRotateLeft(rq, parent);
                w = parent->right;
            }
            if (w && (!w->left || w->left->color == 0) && (!w->right || w->right->color == 0)) {
                if (w) w->color = 1;
                x = parent;
                parent = x->parent;
            } else {
                if (w && (!w->right || w->right->color == 0)) {
                    if (w->left) w->left->color = 0;
                    w->color = 1;
                    EEVDFRBRotateRight(rq, w);
                    w = parent->right;
                }
                if (w) {
                    w->color = parent->color;
                    parent->color = 0;
                    if (w->right) w->right->color = 0;
                }
                EEVDFRBRotateLeft(rq, parent);
                x = rq->rb_root;
            }
        } else {
            EEVDFRBNode* w = parent->left;
            if (w && w->color == 1) {
                w->color = 0;
                parent->color = 1;
                EEVDFRBRotateRight(rq, parent);
                w = parent->left;
            }
            if (w && (!w->right || w->right->color == 0) && (!w->left || w->left->color == 0)) {
                if (w) w->color = 1;
                x = parent;
                parent = x->parent;
            } else {
                if (w && (!w->left || w->left->color == 0)) {
                    if (w->right) w->right->color = 0;
                    w->color = 1;
                    EEVDFRBRotateLeft(rq, w);
                    w = parent->left;
                }
                if (w) {
                    w->color = parent->color;
                    parent->color = 0;
                    if (w->left) w->left->color = 0;
                }
                EEVDFRBRotateRight(rq, parent);
                x = rq->rb_root;
            }
        }
    }
    if (x) x->color = 0;
}

// Remove from red-black tree
static void EEVDFRBDelete(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p) {
    EEVDFRBNode* node = p->rb_node;
    if (!node) return;
    
    // Update leftmost if necessary
    if (rq->rb_leftmost == node) {
        if (node->right) {
            rq->rb_leftmost = EEVDFRBFirst(node->right);
        } else {
            // Find the next leftmost node by walking up and finding next in-order
            const EEVDFRBNode * current = node;
            EEVDFRBNode* parent = current->parent;
            while (parent && current == parent->right) {
                current = parent;
                parent = parent->parent;
            }
            rq->rb_leftmost = parent;
        }
    }
    
    EEVDFRBNode* y = node;
    EEVDFRBNode* x;
    EEVDFRBNode* x_parent;
    uint8_t y_original_color = y->color;
    
    if (!node->left) {
        x = node->right;
        x_parent = node->parent;
        if (!node->parent) {
            rq->rb_root = node->right;
        } else if (node == node->parent->left) {
            node->parent->left = node->right;
        } else {
            node->parent->right = node->right;
        }
        if (node->right) node->right->parent = node->parent;
    } else if (!node->right) {
        x = node->left;
        x_parent = node->parent;
        if (!node->parent) {
            rq->rb_root = node->left;
        } else if (node == node->parent->left) {
            node->parent->left = node->left;
        } else {
            node->parent->right = node->left;
        }
        node->left->parent = node->parent;
    } else {
        // Find successor
        y = node->right;
        while (y->left) y = y->left;
        
        y_original_color = y->color;
        x = y->right;
        x_parent = y->parent;
        
        if (y->parent == node) {
            x_parent = y;
        } else {
            if (y->right) y->right->parent = y->parent;
            y->parent->left = y->right;
            y->right = node->right;
            y->right->parent = y;
            x_parent = y->parent;
        }
        
        if (!node->parent) {
            rq->rb_root = y;
        } else if (node == node->parent->left) {
            node->parent->left = y;
        } else {
            node->parent->right = y;
        }
        
        y->parent = node->parent;
        y->color = node->color;
        y->left = node->left;
        y->left->parent = y;
    }
    
    if (y_original_color == 0) {
        EEVDFRBDeleteFixup(rq, x, x_parent);
    }
    
    EEVDFFreeRBNode(node);
    p->rb_node = NULL;
}

// =============================================================================
// Task Queue Management
// =============================================================================

void EEVDFEnqueueTask(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p) {
    if (!p || p->state != PROC_READY) return;
    
    // Place newly woken tasks at min_vruntime to ensure fairness
    if (p->vruntime < rq->min_vruntime) {
        p->vruntime = rq->min_vruntime;
    }
    
    // Calculate deadline
    p->deadline = p->vruntime + EEVDFCalcSlice(rq, p);
    
    // Update load
    rq->load_weight += p->weight;
    rq->nr_running++;
    
    // Insert into red-black tree
    EEVDFRBInsert(rq, p);
}

void EEVDFDequeueTask(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p) {
    if (!p || !p->rb_node) return;
    
    // Update load
    if (rq->load_weight >= p->weight) {
        rq->load_weight -= p->weight;
    } else {
        rq->load_weight = 0;
    }
    
    if (rq->nr_running > 0) {
        rq->nr_running--;
    }
    
    // Remove from red-black tree
    EEVDFRBDelete(rq, p);
}

EEVDFProcessControlBlock* EEVDFPickNext(EEVDFRunqueue* rq) {
    if (!rq->rb_leftmost) return NULL;
    
    EEVDFProcessControlBlock* next = &processes[rq->rb_leftmost->slot];
    
    // EEVDF: Pick earliest eligible with earliest deadline
    // For simplicity, we pick the leftmost (earliest vruntime)
    // A full EEVDF implementation would check eligibility based on current virtual time
    
    return next;
}

// =============================================================================
// Process Management (same security model as MLFQ)
// =============================================================================

static uint64_t EEVDFSecureHash(const void* data, const uint64_t len, uint64_t salt) {
    const uint8_t* bytes = data;
    uint64_t hash = salt;
    
    for (uint64_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL; // FNV-1a prime
    }
    
    return hash;
}

static uint64_t EEVDFCalculateSecureChecksum(const EEVDFSecurityToken* token, uint32_t pid) {
    uint64_t base_hash = EEVDFSecureHash(token, offsetof(EEVDFSecurityToken, checksum), EEVDF_SECURITY_SALT);
    uint64_t pid_hash = EEVDFSecureHash(&pid, sizeof(pid), EEVDF_SECURITY_SALT);
    return base_hash ^ pid_hash;
}

static inline int FindFreeSlotFast(void) {
    if (UNLIKELY(active_process_bitmap == ~1ULL)) {
        return -1;
    }
    
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

// Add to termination queue (same as MLFQ)
static void AddToTerminationQueueAtomic(uint32_t slot) {
    uint32_t tail = term_queue_tail;
    uint32_t new_tail = (tail + 1) % EEVDF_MAX_PROCESSES;
    
    if (UNLIKELY(term_queue_count >= EEVDF_MAX_PROCESSES)) {
        PANIC("EEVDF: Termination queue overflow");
    }
    
    termination_queue[tail] = slot;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    term_queue_tail = new_tail;
    AtomicInc(&term_queue_count);
}

static uint32_t RemoveFromTerminationQueueAtomic(void) {
    if (UNLIKELY(term_queue_count == 0)) {
        return EEVDF_MAX_PROCESSES;
    }
    
    uint32_t head = term_queue_head;
    uint32_t slot = termination_queue[head];
    
    term_queue_head = (head + 1) % EEVDF_MAX_PROCESSES;
    AtomicDec(&term_queue_count);
    
    return slot;
}

void ProcessExitStubEEVDF() {
    EEVDFProcessControlBlock* current = EEVDFGetCurrentProcess();
    
    PrintKernel("\nEEVDF: Process PID ");
    PrintKernelInt(current->pid);
    PrintKernel(" exited normally\n");
    
    // Terminate process
    EEVDFKillCurrentProcess("Normal exit");
    
    while (1) {
        __asm__ __volatile__("hlt");
    }
    __builtin_unreachable();
}

// =============================================================================
// Core Scheduler Functions
// =============================================================================

void EEVDFUpdateClock(EEVDFRunqueue* rq) {
    rq->clock = GetNS();
    rq->exec_clock = rq->clock;
}

void EEVDFSchedule(Registers* regs) {
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);

    AtomicInc(&scheduler_calls);
    AtomicInc(&eevdf_scheduler.tick_counter);
    
    EEVDFRunqueue* rq = &eevdf_scheduler.rq;
    uint32_t old_slot = rq->current_slot;
    EEVDFProcessControlBlock* prev = NULL;
    
    // Update clock
    EEVDFUpdateClock(rq);

    // Handle current task
    if (LIKELY(old_slot != 0 && old_slot < EEVDF_MAX_PROCESSES)) {
        prev = &processes[old_slot];
        
        if (UNLIKELY(prev->state == PROC_DYING || prev->state == PROC_ZOMBIE || prev->state == PROC_TERMINATED)) {
            goto pick_next;
        }
        
        // Save context
        FastMemcpy(&prev->context, regs, sizeof(Registers));
        
        // Update runtime statistics
        EEVDFUpdateCurr(rq, prev);

        // Check if task should continue running
        if (LIKELY(prev->state == PROC_RUNNING)) {
            prev->state = PROC_READY;
            ready_process_bitmap |= (1ULL << old_slot);
            
            // Re-enqueue the task (it was dequeued when it started running)
            EEVDFEnqueueTask(rq, prev);
        }
    }
    
pick_next:;
    EEVDFProcessControlBlock* next = EEVDFPickNext(rq);
    uint32_t next_slot;
    
    if (UNLIKELY(!next)) {
        next_slot = 0; // Idle process
    } else {
        next_slot = next - processes;
        
        if (UNLIKELY(next_slot >= EEVDF_MAX_PROCESSES || next->state != PROC_READY)) {
            // Remove invalid process from tree to prevent infinite loop
            EEVDFDequeueTask(rq, next);
            goto pick_next;
        }
        
        // Dequeue the selected task
        EEVDFDequeueTask(rq, next);
    }

    // Context switch
    rq->current_slot = next_slot;
    current_process = next_slot;
    
    if (LIKELY(next_slot != 0)) {
        EEVDFProcessControlBlock* new_proc = &processes[next_slot];
        
        // Validate process before switching
        if (UNLIKELY(!new_proc->stack || new_proc->context.rip == 0)) {
            // Invalid process, fall back to idle
            next_slot = 0;
            goto switch_to_idle;
        }
        
        new_proc->state = PROC_RUNNING;
        ready_process_bitmap &= ~(1ULL << next_slot);
        
        new_proc->exec_start = GetNS();
        new_proc->slice_ns = EEVDFCalcSlice(rq, new_proc);
        
        FastMemcpy(regs, &new_proc->context, sizeof(Registers));

        AtomicInc(&context_switches);
        eevdf_scheduler.switch_count++;
    }

switch_to_idle:

    SpinUnlockIrqRestore(&scheduler_lock, flags);
}

int EEVDFSchedInit(void) {
    PrintKernel("System: Initializing EEVDF scheduler...\n");
    // Initialize process array
    FastMemset(processes, 0, sizeof(EEVDFProcessControlBlock) * EEVDF_MAX_PROCESSES);
    
    // Initialize scheduler
    FastMemset(&eevdf_scheduler, 0, sizeof(EEVDFScheduler));
    
    // Initialize RB tree node pool
    FastMemset(rb_node_pool, 0, sizeof(rb_node_pool));
    FastMemset(rb_node_pool_bitmap, 0, sizeof(rb_node_pool_bitmap));
    
    // Initialize runqueue
    EEVDFRunqueue* rq = &eevdf_scheduler.rq;
    rq->rb_root = NULL;
    rq->rb_leftmost = NULL;
    rq->min_vruntime = 0;
    rq->load_weight = 0;
    rq->nr_running = 0;
    rq->current_slot = 0;
    
    eevdf_scheduler.tick_counter = 1;
    eevdf_scheduler.total_processes = 0;
    eevdf_scheduler.context_switch_overhead = 5;
    
    // Initialize idle process
    EEVDFProcessControlBlock* idle_proc = &processes[0];
    FormatA(idle_proc->name, sizeof(idle_proc->name), "Idle");
    idle_proc->pid = 0;
    idle_proc->state = PROC_RUNNING;
    idle_proc->privilege_level = EEVDF_PROC_PRIV_SYSTEM;
    idle_proc->creation_time = EEVDFGetSystemTicks();
    EEVDFSetTaskNice(idle_proc, 0);
    idle_proc->vruntime = 0;
    idle_proc->exec_start = GetNS();
    
    // Initialize idle process security token
    EEVDFSecurityToken* token = &idle_proc->token;
    token->magic = EEVDF_SECURITY_MAGIC;
    token->creator_pid = 0;
    token->privilege = EEVDF_PROC_PRIV_SYSTEM;
    token->flags = EEVDF_PROC_FLAG_CORE;
    token->creation_tick = idle_proc->creation_time;
    token->checksum = 0;
    token->checksum = EEVDFCalculateSecureChecksum(token, 0);
    
    FormatA(idle_proc->ProcessRuntimePath, sizeof(idle_proc->ProcessRuntimePath), "%s/%d", RuntimeServices, idle_proc->pid);
    
    process_count = 1;
    active_process_bitmap |= 1ULL;

#ifdef VF_CONFIG_USE_VFSHELL
    // Create shell process
    PrintKernel("System: Creating shell process...\n");
    const uint32_t shell_pid = EEVDFCreateProcess("VFShell", ShellProcess);
    if (!shell_pid) {
#ifndef VF_CONFIG_PANIC_OVERRIDE
        PANIC("CRITICAL: Failed to create shell process");
#else
        PrintKernelError("CRITICAL: Failed to create shell process\n");
#endif
    }
    PrintKernelSuccess("System: Shell created with PID: ");
    PrintKernelInt(shell_pid);
    PrintKernel("\n");
#endif

    PrintKernelSuccess("System: EEVDF scheduler initialized\n");
    return 0;
}

uint32_t EEVDFCreateProcess(const char* name, void (*entry_point)(void)) {
    if (UNLIKELY(!entry_point)) {
        PANIC("EEVDFCreateProcess: NULL entry point");
    }
    
    irq_flags_t flags = SpinLockIrqSave(&scheduler_lock);
    
    if (UNLIKELY(process_count >= EEVDF_MAX_PROCESSES)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("EEVDFCreateProcess: Too many processes");
    }

    // Find free slot
    int slot = FindFreeSlotFast();
    if (UNLIKELY(slot == -1)) {
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("EEVDFCreateProcess: No free process slots");
    }
    // Allocate PID
    uint32_t new_pid = 0;
    SpinLock(&pid_lock);
    for (int i = 1; i < EEVDF_MAX_PROCESSES; i++) {
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
        PANIC("EEVDFCreateProcess: PID exhaustion");
    }
    
    // Clear process structure
    FastMemset(&processes[slot], 0, sizeof(EEVDFProcessControlBlock));

    // Allocate stack
    void* stack = VMemAllocStack(EEVDF_STACK_SIZE);
    if (UNLIKELY(!stack)) {
        FreeSlotFast(slot);
        SpinUnlockIrqRestore(&scheduler_lock, flags);
        PANIC("EEVDFCreateProcess: Failed to allocate stack");
    }

    EEVDFProcessControlBlock* creator = EEVDFGetCurrentProcess();

    // Initialize process
    EEVDFProcessControlBlock* proc = &processes[slot];
    FormatA(proc->name, sizeof(proc->name), "%s", name ? name : FormatS("proc%d", slot));
    proc->pid = new_pid;
    proc->state = PROC_READY;
    proc->stack = stack;
    proc->privilege_level = EEVDF_PROC_PRIV_NORM;
    proc->creation_time = EEVDFGetSystemTicks();
    EEVDFSetTaskNice(proc, EEVDF_DEFAULT_NICE);

    // Set virtual time to current minimum to ensure fairness
    proc->vruntime = eevdf_scheduler.rq.min_vruntime;
    proc->exec_start = GetNS();

    // Initialize security token
    EEVDFSecurityToken* token = &proc->token;
    token->magic = EEVDF_SECURITY_MAGIC;
    token->creator_pid = creator->pid;
    token->privilege = EEVDF_PROC_PRIV_NORM;
    token->flags = EEVDF_PROC_FLAG_NONE;
    token->creation_tick = proc->creation_time;
    token->checksum = EEVDFCalculateSecureChecksum(token, new_pid);

    // Set up context
    uint64_t rsp = (uint64_t)stack;
    rsp &= ~0xF; // 16-byte alignment

    // Push exit stub as return address
    rsp -= 8;
    *(uint64_t*)rsp = (uint64_t)ProcessExitStubEEVDF;

    proc->context.rsp = rsp;
    proc->context.rip = (uint64_t)entry_point;
    proc->context.rflags = 0x202;
    proc->context.cs = 0x08;
    proc->context.ss = 0x10;
    
    // Initialize IPC queue
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;

    FormatA(proc->ProcessRuntimePath, sizeof(proc->ProcessRuntimePath), "%s/%d", RuntimeProcesses, new_pid);
    
    // Update counters
    __sync_fetch_and_add(&process_count, 1);
    ready_process_bitmap |= (1ULL << slot);
    eevdf_scheduler.total_processes++;

    // Add to scheduler
    EEVDFEnqueueTask(&eevdf_scheduler.rq, proc);

    SpinUnlockIrqRestore(&scheduler_lock, flags);

    return new_pid;
}

EEVDFProcessControlBlock* EEVDFGetCurrentProcess(void) {
    if (current_process >= EEVDF_MAX_PROCESSES) {
        PANIC("EEVDFGetCurrentProcess: Invalid current process index");
    }
    return &processes[current_process];
}

EEVDFProcessControlBlock* EEVDFGetCurrentProcessByPID(uint32_t pid) {
    ReadLock(&process_table_rwlock_eevdf, pid);
    for (int i = 0; i < EEVDF_MAX_PROCESSES; i++) {
        if (processes[i].pid == pid && processes[i].state != PROC_TERMINATED) {
            EEVDFProcessControlBlock* found = &processes[i];
            ReadUnlock(&process_table_rwlock_eevdf, pid);
            return found;
        }
    }
    ReadUnlock(&process_table_rwlock_eevdf, pid);
    return NULL;
}

void EEVDFYield(void) {
    // Simple yield - just request a schedule
    need_schedule = 1;
    volatile int delay = eevdf_scheduler.total_processes * 100;
    while (delay-- > 0) __asm__ __volatile__("pause");
}

void EEVDFKillProcess(uint32_t pid) {
    // TODO: Implement process termination (similar to MLFQ)
    PrintKernel("EEVDF: Kill process not fully implemented yet\n");
}

void EEVDFKillCurrentProcess(const char* reason) {
    // TODO: Implement current process termination
    PrintKernel("EEVDF: Kill current process not fully implemented yet: ");
    PrintKernel(reason);
    PrintKernel("\n");
}

void EEVDFProcessBlocked(uint32_t slot) {
    if (slot >= EEVDF_MAX_PROCESSES) return;
    
    EEVDFProcessControlBlock* proc = &processes[slot];
    proc->io_operations++;
    
    if (slot == eevdf_scheduler.rq.current_slot) {
        need_schedule = 1;
    }
}

void EEVDFWakeupTask(EEVDFProcessControlBlock* p) {
    if (!p || p->state != PROC_BLOCKED) return;
    
    p->state = PROC_READY;
    p->last_wakeup = GetNS();
    
    // Add back to runqueue
    EEVDFEnqueueTask(&eevdf_scheduler.rq, p);
}

void EEVDFCleanupTerminatedProcess(void) {
    // TODO: Implement cleanup (similar to MLFQ)
}

// =============================================================================
// Statistics and Debugging
// =============================================================================

void EEVDFDumpSchedulerState(void) {
    PrintKernel("[EEVDF] Current slot: ");
    PrintKernelInt(eevdf_scheduler.rq.current_slot);
    PrintKernel(" Nr running: ");
    PrintKernelInt(eevdf_scheduler.rq.nr_running);
    PrintKernel(" Load weight: ");
    PrintKernelInt(eevdf_scheduler.rq.load_weight);
    PrintKernel("\n[EEVDF] Min vruntime: ");
    PrintKernelInt((uint32_t)eevdf_scheduler.rq.min_vruntime);
    PrintKernel(" Total processes: ");
    PrintKernelInt(eevdf_scheduler.total_processes);
    PrintKernel(" Context switches: ");
    PrintKernelInt((uint32_t)eevdf_scheduler.switch_count);
    PrintKernel("\n");
}

void EEVDFListProcesses(void) {
    PrintKernel("\n--- EEVDF Process List ---\n");
    PrintKernel("PID\tState     \tNice\tWeight\tVRuntime\tCPU Time\tName\n");
    PrintKernel("---------------------------------------------------------------\n");
    
    for (int i = 0; i < EEVDF_MAX_PROCESSES; i++) {
        if (i == 0 || processes[i].pid != 0) {
            const EEVDFProcessControlBlock* p = &processes[i];
            
            PrintKernelInt(p->pid);
            PrintKernel("\t");
            switch (p->state) {
                case PROC_TERMINATED: PrintKernel("TERMINATED"); break;
                case PROC_READY:      PrintKernel("READY     "); break;
                case PROC_RUNNING:    PrintKernel("RUNNING   "); break;
                case PROC_BLOCKED:    PrintKernel("BLOCKED   "); break;
                case PROC_ZOMBIE:     PrintKernel("ZOMBIE    "); break;
                case PROC_DYING:      PrintKernel("DYING     "); break;
                default:              PrintKernel("UNKNOWN   "); break;
            }
            PrintKernel("\t");
            PrintKernelInt(p->nice);
            PrintKernel("\t");
            PrintKernelInt(p->weight);
            PrintKernel("\t");
            PrintKernelInt((uint32_t)p->vruntime);
            PrintKernel("\t");
            PrintKernelInt((uint32_t)p->cpu_time_accumulated);
            PrintKernel("\t");
            PrintKernel(p->name);
            PrintKernel("\n");
        }
    }
    PrintKernel("---------------------------------------------------------------\n");
}

void EEVDFGetProcessStats(uint32_t pid, uint32_t* cpu_time, uint32_t* wait_time, uint32_t* preemptions) {
    ReadLock(&process_table_rwlock_eevdf, pid);
    EEVDFProcessControlBlock* proc = EEVDFGetCurrentProcessByPID(pid);
    if (!proc) {
        if (cpu_time) *cpu_time = 0;
        if (wait_time) *wait_time = 0;
        if (preemptions) *preemptions = 0;
        ReadUnlock(&process_table_rwlock_eevdf, pid);
        return;
    }
    
    if (cpu_time) *cpu_time = (uint32_t)proc->cpu_time_accumulated;
    if (wait_time) *wait_time = (uint32_t)proc->wait_sum;
    if (preemptions) *preemptions = proc->preemption_count;
    ReadUnlock(&process_table_rwlock_eevdf, pid);
}

void EEVDFDumpPerformanceStats(void) {
    PrintKernel("[EEVDF-PERF] Context switches: ");
    PrintKernelInt((uint32_t)context_switches);
    PrintKernel("\n[EEVDF-PERF] Scheduler calls: ");
    PrintKernelInt((uint32_t)scheduler_calls);
    PrintKernel("\n[EEVDF-PERF] Active processes: ");
    PrintKernelInt(__builtin_popcountll(active_process_bitmap));
    PrintKernel("\n[EEVDF-PERF] Switch count: ");
    PrintKernelInt((uint32_t)eevdf_scheduler.switch_count);
    PrintKernel("\n[EEVDF-PERF] Migration count: ");
    PrintKernelInt((uint32_t)eevdf_scheduler.migration_count);
    PrintKernel("\n");
}

// Preemption check
int EEVDFCheckPreempt(EEVDFRunqueue* rq, EEVDFProcessControlBlock* p) {
    EEVDFProcessControlBlock* curr = &processes[rq->current_slot];
    
    // Always preempt if no current task or idle task
    if (rq->current_slot == 0 || !curr) return 1;
    
    // Check if new task has earlier deadline (simplified EEVDF)
    if (p->deadline < curr->deadline) return 1;
    
    // Check if new task has significantly lower vruntime
    if (p->vruntime + EEVDF_WAKEUP_GRANULARITY < curr->vruntime) return 1;
    
    return 0;
}

void EEVDFYieldTask(EEVDFRunqueue* rq) {
    if (rq->current_slot == 0) return;
    
    EEVDFProcessControlBlock* curr = &processes[rq->current_slot];
    
    // Update current task and yield
    EEVDFUpdateCurr(rq, curr);
    curr->state = PROC_READY;
    
    // Re-enqueue with updated vruntime
    EEVDFEnqueueTask(rq, curr);
    
    need_schedule = 1;
}