#include <EEVDF.h>
#include <Atomics.h>
#ifdef VF_CONFIG_USE_CERBERUS
#include <Cerberus.h>
#endif
#include <Console.h>
#include <Format.h>
#include <Gdt.h>
#include <Ipc.h>
#include <MemOps.h>
#include <Panic.h>
#include <Shell.h>
#include <SpinlockRust.h>
#include <VFS.h>
#include <VMem.h>
#include <x64.h>
#include <procfs/ProcFS.h>
#include <CRC32.h>

#define offsetof(type, member) ((uint64_t)&(((type*)0)->member))

// Performance optimizations
#define LIKELY(x)               __builtin_expect(!!(x), 1)
#define UNLIKELY(x)             __builtin_expect(!!(x), 0)
#define CACHE_LINE_SIZE         64
#define ALIGNED_CACHE           __attribute__((aligned(CACHE_LINE_SIZE)))

// SIS (Scheduler Integrated Security) constants
static const uint64_t SIS_MAGIC = 0x5EC0DE4D41474943ULL;
static const uint64_t SIS_SALT_BASE = 0xDEADBEEFCAFEBABEULL;
static const uint32_t SIS_MAX_VIOLATIONS = EEVDF_SECURITY_VIOLATION_LIMIT;

// SIS runtime state
static volatile uint64_t sis_global_nonce = 0x1337C0DEDEADBEEFULL;
static volatile uint64_t sis_boot_entropy = 0;
static uint64_t sis_process_keys[EEVDF_MAX_PROCESSES] ALIGNED_CACHE;

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

// Per-process locks for fine-grained locking
static RustSpinLock* process_locks[EEVDF_MAX_PROCESSES] ALIGNED_CACHE;

// Global state with atomic operations
static EEVDFProcessControlBlock processes[EEVDF_MAX_PROCESSES] ALIGNED_CACHE;
static uint64_t pid_bitmap[EEVDF_MAX_PROCESSES / 64 + 1] = {0};
static volatile uint32_t current_process = 0;
static volatile uint32_t process_count = 0;
static volatile int need_schedule = 0;
static RustSpinLock* pid_lock = NULL;
static RustSpinLock* runqueue_lock = NULL;  // Only for RB tree operations

// Security subsystem
uint32_t eevdf_security_manager_pid = 0;
static volatile uint32_t security_violation_count = 0;

// Lockless bitmaps with atomic operations
static volatile uint64_t active_process_bitmap = 0;
static volatile uint64_t ready_process_bitmap = 0;

// Main scheduler instance
static EEVDFScheduler eevdf_scheduler ALIGNED_CACHE;
static EEVDFRBNode rb_node_pool[EEVDF_MAX_PROCESSES] ALIGNED_CACHE;
static volatile uint32_t rb_node_pool_bitmap[(EEVDF_MAX_PROCESSES + 31) / 32];

// Lockless termination queue with atomic operations
static volatile uint32_t termination_queue[EEVDF_MAX_PROCESSES];
static volatile uint32_t term_queue_head = 0;
static volatile uint32_t term_queue_tail = 0;
static volatile uint32_t term_queue_count = 0;

// Performance counters (atomic)
static volatile uint64_t context_switches = 0;
static volatile uint64_t scheduler_calls = 0;

extern volatile uint32_t APIC_HZ;
extern volatile uint32_t APICticks;

// =============================================================================
// SIS (Scheduler Integrated Security) - Ultra Low Overhead
// =============================================================================

// Fast CRC32 hash using existing crypto/CRC32.h
static inline uint64_t SISFastHash(uint64_t a, uint64_t b) {
    uint64_t combined[2] = {a, b};
    return CRC32(combined, sizeof(combined));
}

// Generate process-specific key (called once at creation)
static uint64_t SISGenerateProcessKey(uint32_t slot, uint32_t pid) {
    AtomicInc64(&sis_global_nonce);
    uint64_t entropy = sis_global_nonce;
    uint64_t key = SISFastHash(SIS_SALT_BASE ^ entropy, (uint64_t)pid << 32 | slot);
    sis_process_keys[slot] = key;
    return key;
}

// Ultra-fast PCB seal (3-4 instructions)
static inline uint64_t SISSealPCB(const EEVDFProcessControlBlock* pcb, uint32_t slot) {
    uint64_t critical = (uint64_t)pcb->pid << 32 | pcb->privilege_level << 16 | pcb->state;
    return SISFastHash(critical, sis_process_keys[slot]);
}

// Ultra-fast PCB verification (2-3 instructions)
static inline int SISVerifyPCB(const EEVDFProcessControlBlock* pcb, uint32_t slot) {
    if (UNLIKELY(slot >= EEVDF_MAX_PROCESSES)) return 0;
    uint64_t expected = SISSealPCB(pcb, slot);
    return LIKELY(pcb->sis_seal == expected);
}

// Update seal after legitimate state change
static inline void SISUpdateSeal(EEVDFProcessControlBlock* pcb, uint32_t slot) {
    pcb->sis_seal = SISSealPCB(pcb, slot);
}

// Foward declaration
static void EEVDFASTerminate(uint32_t pid, const char* reason);
static void EEVDFCleanupTerminatedProcessInternal(void);
static void EEVDFTerminateProcess(uint32_t pid, TerminationReason reason, uint32_t exit_code);
static int EEVDFPostflightCheck(uint32_t slot);
static int EEVDFPreflightCheck(uint32_t slot);
// =============================================================================
// Utility Functions
// =============================================================================

static int FastFFS(const uint64_t value) {
    return __builtin_ctzll(value);
}

static uint64_t GetNS(void) {
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
    
    p->nice = (int8_t)nice;
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
    for (uint32_t word = 0; word < (EEVDF_MAX_PROCESSES + 31) / 32; word++) {
        uint32_t available = ~rb_node_pool_bitmap[word];
        if (available) {
            int bit = __builtin_ctz(available);
            uint32_t index = word * 32 + bit;
            if (index < EEVDF_MAX_PROCESSES) {
                rb_node_pool_bitmap[word] |= (1U << bit);
                EEVDFRBNode* node = &rb_node_pool[index];
                EEVDFRBNodeInit(node, slot);
                return node;
            }
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

// Legacy compatibility functions (minimal overhead)
static uint64_t EEVDFCalculateTokenChecksum(const EEVDFSecurityToken* token) {
    return SISFastHash(token->magic, token->capabilities);
}

static uint64_t EEVDFCalculatePCBHash(const EEVDFProcessControlBlock* pcb) {
    return pcb->sis_seal;
}

static inline int FindFreeSlotFast(void) {
    uint64_t current = AtomicRead64(&active_process_bitmap);
    if (UNLIKELY(current == ~1ULL)) {
        return -1;
    }
    
    uint64_t available = ~current;
    available &= ~1ULL; // Clear bit 0
    
    if (UNLIKELY(available == 0)) {
        return -1;
    }
    
    int slot = FastFFS(available);
    uint64_t mask = 1ULL << slot;
    
    // Atomic compare-and-swap to claim the slot
    if (AtomicCmpxchg64(&active_process_bitmap, current, current | mask) == current) {
        return slot;
    }
    
    // Retry if CAS failed
    return FindFreeSlotFast();
}

static inline void FreeSlotFast(int slot) {
    if (LIKELY(slot > 0 && slot < 64)) {
        uint64_t mask = 1ULL << slot;
        AtomicFetchAnd64(&active_process_bitmap, ~mask);
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
    
    if (UNLIKELY(!current)) {
        PrintKernelError("EEVDF: ProcessExitStub called with null current process\n");
        while (1) {
            __asm__ __volatile__("hlt");
        }
    }
    
    PrintKernel("\nEEVDF: Process PID ");
    PrintKernelInt(current->pid);
    PrintKernel(" exited normally\n");
    
    // Use direct termination to avoid potential recursion issues
    EEVDFTerminateProcess(current->pid, TERM_NORMAL, 0);
    
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
    // Pre-compute values outside lock
    uint64_t now = GetNS();
    uint32_t old_slot = eevdf_scheduler.rq.current_slot;
    
    AtomicInc64(&scheduler_calls);
    AtomicInc(&eevdf_scheduler.tick_counter);
    
    // Only lock runqueue for tree operations, not the entire scheduler
    uint64_t flags = 0;
    int need_rq_lock = 0;

#ifdef VF_CONFIG_USE_CERBERUS
    static uint64_t cerberus_tick_counter = 0;
    if (++cerberus_tick_counter % 10 == 0) {
        CerberusTick();
    }
#endif

    EEVDFRunqueue* rq = &eevdf_scheduler.rq;
    EEVDFProcessControlBlock* prev = NULL;
    rq->clock = now;
    rq->exec_clock = now;

    // Handle current task without global lock
    if (LIKELY(old_slot != 0 && old_slot < EEVDF_MAX_PROCESSES)) {
        prev = &processes[old_slot];
        
        ProcessState state = AtomicRead((volatile uint32_t*)&prev->state);
        if (UNLIKELY(state == PROC_DYING || state == PROC_ZOMBIE || state == PROC_TERMINATED)) {
            goto pick_next;
        }

        if (UNLIKELY(!EEVDFPostflightCheck(old_slot))) {
            goto pick_next;
        }
        
        // Save context (lockless)
        FastMemcpy(&prev->context, regs, sizeof(Registers));
        
        // Update runtime statistics (lockless)
        EEVDFUpdateCurr(rq, prev);

        // Atomic state transition with SIS update
        if (LIKELY(AtomicCmpxchg((volatile uint32_t*)&prev->state, PROC_RUNNING, PROC_READY) == PROC_RUNNING)) {
            SISUpdateSeal(prev, old_slot); // Update seal after state change
            AtomicFetchOr64(&ready_process_bitmap, 1ULL << old_slot);
            need_rq_lock = 1;
        }
    }
    
pick_next:;
    // Lock runqueue only when needed for tree operations
    if (need_rq_lock || prev) {
        flags = rust_spinlock_lock_irqsave(runqueue_lock);
        
        // Re-enqueue previous task if needed
        if (prev && prev->state == PROC_READY) {
            EEVDFEnqueueTask(rq, prev);
        }
    }
    
    EEVDFProcessControlBlock* next = EEVDFPickNext(rq);
    uint32_t next_slot;
    
    if (UNLIKELY(!next)) {
        next_slot = 0; // Idle process
    } else {
        next_slot = next - processes;

        if (UNLIKELY(!EEVDFPreflightCheck(next_slot))) {
            EEVDFDequeueTask(rq, next);
            next = EEVDFPickNext(rq); // Try again
            next_slot = next ? (next - processes) : 0;
        }
        
        if (LIKELY(next_slot < EEVDF_MAX_PROCESSES && next && next->state == PROC_READY)) {
            EEVDFDequeueTask(rq, next);
        } else {
            next_slot = 0; // Fall back to idle
        }
    }
    
    if (need_rq_lock || prev) {
        rust_spinlock_unlock_irqrestore(runqueue_lock, flags);
    }

    // Context switch (lockless)
    rq->current_slot = next_slot;
    AtomicStore(&current_process, next_slot);
    
    if (LIKELY(next_slot != 0)) {
        EEVDFProcessControlBlock* new_proc = &processes[next_slot];
        
        // Validate process before switching
        if (UNLIKELY(!new_proc->stack || new_proc->context.rip == 0)) {
            next_slot = 0;
            goto switch_to_idle;
        }
        
        AtomicStore((volatile uint32_t*)&new_proc->state, PROC_RUNNING);
        SISUpdateSeal(new_proc, next_slot); // Update seal after state change
        AtomicFetchAnd64(&ready_process_bitmap, ~(1ULL << next_slot));
        
        new_proc->exec_start = GetNS();
        new_proc->slice_ns = EEVDFCalcSlice(rq, new_proc);
        
        FastMemcpy(regs, &new_proc->context, sizeof(Registers));

        AtomicInc64(&context_switches);
        AtomicInc(&eevdf_scheduler.switch_count);
    }

switch_to_idle:;
    
    // Cleanup outside lock to reduce critical section
    if (UNLIKELY((scheduler_calls % 100) == 0)) {
        EEVDFCleanupTerminatedProcess();
    }
}

int EEVDFSchedInit(void) {
    if (!runqueue_lock && !pid_lock) {
        runqueue_lock = rust_spinlock_new();
        pid_lock = rust_spinlock_new();
        if (!runqueue_lock || !pid_lock) PANIC("EEVDFSchedInit: Failed to allocate locks");
        
        // Initialize per-process locks
        for (int i = 0; i < EEVDF_MAX_PROCESSES; i++) {
            process_locks[i] = rust_spinlock_new();
            if (!process_locks[i]) PANIC("EEVDFSchedInit: Failed to allocate process locks");
        }
    }
    PrintKernel("System: Initializing EEVDF scheduler...\n");
    
#ifdef VF_CONFIG_USE_CERBERUS
    // Initialize Cerberus
    CerberusInit();
#endif
    
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
    snprintf(idle_proc->name, sizeof(idle_proc->name), "Idle");
    idle_proc->pid = 0;
    idle_proc->state = PROC_RUNNING;
    idle_proc->privilege_level = EEVDF_PROC_PRIV_SYSTEM;
    idle_proc->creation_time = EEVDFGetSystemTicks();
    EEVDFSetTaskNice(idle_proc, 0);
    idle_proc->vruntime = 0;
    idle_proc->exec_start = GetNS();
    idle_proc->initial_entry_point = 0; // Idle process has no specific entry point
    
    // Initialize SIS for idle process
    sis_boot_entropy = GetNS() ^ (uint64_t)&idle_proc; // Boot-time entropy
    SISGenerateProcessKey(0, 0);
    
    EEVDFSecurityToken* token = &idle_proc->token;
    token->magic = SIS_MAGIC;
    token->creator_pid = 0;
    token->privilege = EEVDF_PROC_PRIV_SYSTEM;
    token->capabilities = EEVDF_CAP_CORE;
    token->creation_tick = idle_proc->creation_time;
    token->checksum = EEVDFCalculateTokenChecksum(token);
    token->pcb_hash = 0; // Will be set by SIS seal
    
    // Seal idle process
    SISUpdateSeal(idle_proc, 0);
    
    snprintf(idle_proc->ProcessRuntimePath, sizeof(idle_proc->ProcessRuntimePath), "%s/%d", RuntimeServices, idle_proc->pid);

    ProcFSRegisterProcess(0, 0);

    process_count = 1;
    active_process_bitmap |= 1ULL;


#ifdef VF_CONFIG_USE_VFSHELL
    // Create shell process
    PrintKernel("System: Creating shell process...\n");
    const uint32_t shell_pid = EEVDFCreateSecureProcess("VFShell", ShellProcess, EEVDF_PROC_PRIV_SYSTEM, EEVDF_CAP_CORE);
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

uint32_t EEVDFCreateSecureProcess(const char* name, void (*entry_point)(void), uint8_t priv, uint64_t capabilities) {
    if (UNLIKELY(!entry_point)) {
        PANIC("EEVDFCreateProcess: NULL entry point");
    }
    
    if (UNLIKELY(AtomicRead(&process_count) >= EEVDF_MAX_PROCESSES)) {
        PANIC("EEVDFCreateProcess: Too many processes");
    }

    // Find free slot (lockless)
    int slot = FindFreeSlotFast();
    if (UNLIKELY(slot == -1)) {
        PANIC("EEVDFCreateProcess: No free process slots");
    }
    // Allocate PID
    uint32_t new_pid = 0;
    rust_spinlock_lock(pid_lock);
    for (int i = 1; i < EEVDF_MAX_PROCESSES; i++) {
        int idx = i / 64;
        int bit = i % 64;
        if (!(pid_bitmap[idx] & (1ULL << bit))) {
            pid_bitmap[idx] |= (1ULL << bit);
            new_pid = i;
            break;
        }
    }
    rust_spinlock_unlock(pid_lock);

    if (new_pid == 0) {
        FreeSlotFast(slot);
        PANIC("EEVDFCreateProcess: PID exhaustion");
    }
    
    // Clear process structure
    FastMemset(&processes[slot], 0, sizeof(EEVDFProcessControlBlock));

    // Allocate stack
    void* stack = VMemAllocStack(EEVDF_STACK_SIZE);
    if (UNLIKELY(!stack)) {
        FreeSlotFast(slot);
        // Free PID
        rust_spinlock_lock(pid_lock);
        int idx = new_pid / 64;
        int bit = new_pid % 64;
        pid_bitmap[idx] &= ~(1ULL << bit);
        rust_spinlock_unlock(pid_lock);
        PANIC("EEVDFCreateProcess: Failed to allocate stack");
    }

    EEVDFProcessControlBlock* creator = EEVDFGetCurrentProcess();

    // Initialize process
    EEVDFProcessControlBlock* proc = &processes[slot];
    snprintf(proc->name, sizeof(proc->name), "%s", name ? name : FormatS("proc%d", slot));
    proc->pid = new_pid;
    proc->state = PROC_READY;
    proc->stack = stack;
    proc->privilege_level = priv;
    proc->creation_time = EEVDFGetSystemTicks();
    EEVDFSetTaskNice(proc, EEVDF_DEFAULT_NICE);
    proc->initial_entry_point = (uint64_t)entry_point; // Store the immutable entry point

    // Set virtual time to current minimum to ensure fairness
    proc->vruntime = eevdf_scheduler.rq.min_vruntime;
    proc->exec_start = GetNS();

    // Generate SIS key and initialize security token
    SISGenerateProcessKey(slot, new_pid);
    
    EEVDFSecurityToken* token = &proc->token;
    token->magic = SIS_MAGIC;
    token->creator_pid = creator->pid;
    token->privilege = priv;
    token->capabilities = capabilities;
    token->creation_tick = proc->creation_time;
    token->checksum = EEVDFCalculateTokenChecksum(token);

    // Set up context
    uint64_t rsp = (uint64_t)stack;
    rsp &= ~0xF; // 16-byte alignment

    // Push exit stub as return address
    rsp -= 8;
    *(uint64_t*)rsp = (uint64_t)ProcessExitStubEEVDF;

    proc->context.rsp = rsp;
    proc->context.rip = (uint64_t)entry_point;
    proc->context.rflags = 0x202;
    proc->context.cs = KERNEL_CODE_SELECTOR;
    proc->context.ss = KERNEL_DATA_SELECTOR;
    
    // Initialize IPC queue
    proc->ipc_queue.head = 0;
    proc->ipc_queue.tail = 0;
    proc->ipc_queue.count = 0;

    snprintf(proc->ProcessRuntimePath, sizeof(proc->ProcessRuntimePath), "%s/%d", RuntimeProcesses, new_pid);

    // Seal process with SIS after all fields are set
    SISUpdateSeal(proc, slot);
    token->pcb_hash = proc->sis_seal;

#ifdef VF_CONFIG_USE_CERBERUS
    CerberusRegisterProcess(new_pid, (uint64_t)stack, EEVDF_STACK_SIZE);
#endif

    ProcFSRegisterProcess(new_pid, stack);

    // Update counters (atomic)
    AtomicInc(&process_count);
    AtomicFetchOr64(&ready_process_bitmap, 1ULL << slot);
    AtomicInc(&eevdf_scheduler.total_processes);

    // Add to scheduler (requires runqueue lock)
    uint64_t rq_flags = rust_spinlock_lock_irqsave(runqueue_lock);
    EEVDFEnqueueTask(&eevdf_scheduler.rq, proc);
    rust_spinlock_unlock_irqrestore(runqueue_lock, rq_flags);

    return new_pid;
}

uint32_t EEVDFCreateProcess(const char* name, void (*entry_point)(void)) {
    return EEVDFCreateSecureProcess(name, entry_point, EEVDF_PROC_PRIV_NORM, EEVDF_CAP_NONE);
}

EEVDFProcessControlBlock* EEVDFGetCurrentProcess(void) {
    if (current_process >= EEVDF_MAX_PROCESSES) {
        PANIC("EEVDFGetCurrentProcess: Invalid current process index");
    }
    return &processes[current_process];
}

EEVDFProcessControlBlock* EEVDFGetCurrentProcessByPID(uint32_t pid) {
    // Lockless search using atomic reads
    for (int i = 0; i < EEVDF_MAX_PROCESSES; i++) {
        if (AtomicRead(&processes[i].pid) == pid && 
            AtomicRead((volatile uint32_t*)&processes[i].state) != PROC_TERMINATED) {
            return &processes[i];
        }
    }
    return NULL;
}

void EEVDFYield(void) {
    // Simple yield - just request a schedule
    need_schedule = 1;
}

// =============================================================================
// Security and Validation Functions
// =============================================================================

// SIS validation - ultra-fast path
static inline int SISValidateProcess(const EEVDFProcessControlBlock* pcb, uint32_t slot) {
    if (UNLIKELY(!pcb || slot >= EEVDF_MAX_PROCESSES)) return 0;
    if (UNLIKELY(pcb->token.magic != SIS_MAGIC)) return 0;
    return SISVerifyPCB(pcb, slot);
}

// Legacy token validation (compatibility)
static int EEVDFValidateToken(const EEVDFSecurityToken* token, const EEVDFProcessControlBlock* pcb) {
    uint32_t slot = pcb - processes;
    return SISValidateProcess(pcb, slot);
}

static inline int EEVDFPreflightCheck(uint32_t slot) {
    if (slot == 0) return 1; // Idle process is always safe

    EEVDFProcessControlBlock* proc = &processes[slot];

    // Ultra-fast SIS check (2-3 instructions)
    if (UNLIKELY(!SISVerifyPCB(proc, slot))) {
        EEVDFASTerminate(proc->pid, "SIS integrity violation");
        return 0;
    }

    // Fast privilege check
    if (UNLIKELY(proc->privilege_level == EEVDF_PROC_PRIV_SYSTEM &&
                 !(proc->token.capabilities & (EEVDF_CAP_SUPERVISOR | EEVDF_CAP_CRITICAL | EEVDF_CAP_IMMUNE)))) {
        EEVDFASTerminate(proc->pid, "Privilege escalation detected");
        return 0;
    }

#ifdef VF_CONFIG_USE_CERBERUS
    CerberusPreScheduleCheck(slot);
#endif

    return 1;
}

static inline int EEVDFPostflightCheck(uint32_t slot) {
    if (slot == 0) return 1; // Idle process is always safe

    EEVDFProcessControlBlock* proc = &processes[slot];

    // Ultra-fast SIS integrity check
    if (UNLIKELY(!SISVerifyPCB(proc, slot))) {
        EEVDFASTerminate(proc->pid, "Runtime PCB corruption detected");
        return 0;
    }

    return 1;
}

// =============================================================================
// Process Termination Functions
// =============================================================================

static void EEVDFTerminateProcess(uint32_t pid, TerminationReason reason, uint32_t exit_code) {
    EEVDFProcessControlBlock* proc = EEVDFGetCurrentProcessByPID(pid);
    if (UNLIKELY(!proc)) return;
    
    uint32_t slot = proc - processes;
    if (slot >= EEVDF_MAX_PROCESSES) return;
    
    // Lock this specific process
    uint64_t flags = rust_spinlock_lock_irqsave(process_locks[slot]);
    
    ProcessState state = AtomicRead((volatile uint32_t*)&proc->state);
    if (UNLIKELY(state == PROC_DYING || state == PROC_ZOMBIE || state == PROC_TERMINATED)) {
        rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
        return;
    }

    EEVDFProcessControlBlock* caller = EEVDFGetCurrentProcess();

    // Enhanced security checks
    if (reason != TERM_SECURITY) {
        // Cross-process termination security
        if (caller->pid != proc->pid) {
            // Only system processes can terminate other processes
            // Check privilege levels - can only kill equal or lower privilege
            if (proc->privilege_level == EEVDF_PROC_PRIV_SYSTEM) {
                // Only system processes can kill system processes
                if (caller->privilege_level != EEVDF_PROC_PRIV_SYSTEM) {
                    rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
                    PrintKernelError("[EEVDF-SECURITY] Process ");
                    PrintKernelInt(caller->pid);
                    PrintKernel(" tried to kill system process ");
                    PrintKernelInt(proc->pid);
                    PrintKernel("\n");
                    EEVDFASTerminate(caller->pid, "Unauthorized system process termination");
                    return;
                }
            }

            // Cannot terminate immune processes
            if (UNLIKELY(proc->token.capabilities & EEVDF_CAP_IMMUNE)) {
                rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
                EEVDFASTerminate(caller->pid, "Attempted termination of immune process");
                return;
            }

            // Cannot terminate critical system processes
            if (UNLIKELY(proc->token.capabilities & EEVDF_CAP_CRITICAL)) {
                rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
                EEVDFASTerminate(caller->pid, "Attempted termination of critical process");
                return;
            }
        }

        // Validate caller's token before allowing termination
        if (UNLIKELY(!EEVDFValidateToken(&caller->token, caller))) {
            rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
            EEVDFASTerminate(caller->pid, "Token validation failed");
            return;
        }
    }

    // Atomic state transition
    if (UNLIKELY(AtomicCmpxchg((volatile uint32_t*)&proc->state, state, PROC_DYING) != state)) {
        rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
        return; // Race condition, another thread is handling termination
    }

    PrintKernel("EEVDF: Terminating PID ");
    PrintKernelInt(pid);
    PrintKernel(" Reason: ");
    PrintKernelInt(reason);
    PrintKernel("\n");

    proc->term_reason = reason;
    proc->exit_code = exit_code;
    proc->termination_time = EEVDFGetSystemTicks();

    // Clear from ready bitmap (atomic)
    AtomicFetchAnd64(&ready_process_bitmap, ~(1ULL << slot));

    // Request immediate reschedule if current process
    if (UNLIKELY(slot == eevdf_scheduler.rq.current_slot)) {
        AtomicStore(&need_schedule, 1);
    }

    AtomicStore((volatile uint32_t*)&proc->state, PROC_ZOMBIE);
    SISUpdateSeal(proc, slot); // Update seal after state change
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    
    rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
    
    // Remove from scheduler (requires runqueue lock)
    uint64_t rq_flags = rust_spinlock_lock_irqsave(runqueue_lock);
    EEVDFDequeueTask(&eevdf_scheduler.rq, proc);
    rust_spinlock_unlock_irqrestore(runqueue_lock, rq_flags);
    
    AddToTerminationQueueAtomic(slot);
    
    // Free PID
    rust_spinlock_lock(pid_lock);
    int idx = proc->pid / 64;
    int bit = proc->pid % 64;
    pid_bitmap[idx] &= ~(1ULL << bit);
    rust_spinlock_unlock(pid_lock);
    
    // Update scheduler statistics (atomic)
    AtomicDec(&eevdf_scheduler.total_processes);

    ProcFSUnregisterProcess(pid);

#ifdef VF_CONFIG_USE_CERBERUS
    CerberusUnregisterProcess(proc->pid);
#endif
}

// EEVDF's deadly termination function - bypasses all protections
static void EEVDFASTerminate(uint32_t pid, const char* reason) {
    EEVDFProcessControlBlock* proc = EEVDFGetCurrentProcessByPID(pid);
    if (!proc) return;
    
    uint32_t slot = proc - processes;
    if (slot >= EEVDF_MAX_PROCESSES) return;
    
    // AS overrides ALL protections - even immune and critical
    uint64_t flags = rust_spinlock_lock_irqsave(process_locks[slot]);
    
    if (AtomicRead((volatile uint32_t*)&proc->state) == PROC_TERMINATED) {
        rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
        return;
    }
    
    AtomicStore((volatile uint32_t*)&proc->state, PROC_DYING);
    proc->term_reason = TERM_SECURITY;
    proc->exit_code = 666; // AS signature
    proc->termination_time = EEVDFGetSystemTicks();

    AtomicFetchAnd64(&ready_process_bitmap, ~(1ULL << slot));

    if (slot == eevdf_scheduler.rq.current_slot) {
        AtomicStore(&need_schedule, 1);
    }

    AtomicStore((volatile uint32_t*)&proc->state, PROC_ZOMBIE);
    SISUpdateSeal(proc, slot); // Update seal for AS termination
    rust_spinlock_unlock_irqrestore(process_locks[slot], flags);
    
    // Remove from scheduler
    uint64_t rq_flags = rust_spinlock_lock_irqsave(runqueue_lock);
    EEVDFDequeueTask(&eevdf_scheduler.rq, proc);
    rust_spinlock_unlock_irqrestore(runqueue_lock, rq_flags);
    
    AddToTerminationQueueAtomic(slot);
    AtomicDec(&eevdf_scheduler.total_processes);
    ProcFSUnregisterProcess(pid);
    
#ifdef VF_CONFIG_USE_CERBERUS
    CerberusUnregisterProcess(proc->pid);
#endif
}

static void EEVDFSecurityViolationHandler(uint32_t violator_pid, const char* reason) {
    PrintKernelError("[EEVDF-SECURITY] Security violation by PID ");
    PrintKernelInt(violator_pid);
    PrintKernelError(": ");
    PrintKernelError(reason);
    PrintKernelError("\n");
    AtomicInc(&security_violation_count);
    uint32_t violations = security_violation_count;
    if (violations >= SIS_MAX_VIOLATIONS) {
        PANIC("SIS: Maximum security violations exceeded");
    }

    EEVDFASTerminate(violator_pid, reason);
}

void EEVDFKillProcess(uint32_t pid) {
    EEVDFTerminateProcess(pid, TERM_KILLED, 1);
}

void EEVDFKillAllProcesses(const char* reason) {
    for (int i = 0; i < EEVDF_MAX_PROCESSES; i++) {
        EEVDFProcessControlBlock* proc = &processes[i];
        if (proc->state != PROC_TERMINATED && proc->pid != 0) {
            EEVDFASTerminate(proc->pid, reason);
        }
    }
}

void EEVDFKillCurrentProcess(const char* reason) {
    EEVDFProcessControlBlock* current = EEVDFGetCurrentProcess();
    if (current) EEVDFASTerminate(current->pid, reason);
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

// Internal cleanup function that assumes scheduler_lock is already held
static void EEVDFCleanupTerminatedProcessInternal(void) {
    // Process a limited number per call to avoid long interrupt delays
    int cleanup_count = 0;
    const int MAX_CLEANUP_PER_CALL = EEVDF_CLEANUP_MAX_PER_CALL;

    while (AtomicRead(&term_queue_count) > 0 && cleanup_count < MAX_CLEANUP_PER_CALL) {
        uint32_t slot = RemoveFromTerminationQueueAtomic();
        if (slot >= EEVDF_MAX_PROCESSES) break;

        EEVDFProcessControlBlock* proc = &processes[slot];
        // Double-check state
        if (proc->state != PROC_ZOMBIE) {
            PrintKernelWarning("EEVDF: Cleanup found non-zombie process (PID: ");
            PrintKernelInt(proc->pid);
            PrintKernelWarning(", State: ");
            PrintKernelInt(proc->state);
            PrintKernelWarning(") in termination queue. Skipping.\n");
            continue;
        }

        PrintKernel("EEVDF: Cleaning up process PID: ");
        PrintKernelInt(proc->pid);
        PrintKernel("\n");

        // Cleanup resources
        if (proc->stack) {
            VMemFreeStack(proc->stack, EEVDF_STACK_SIZE);
            proc->stack = NULL;
        }

        // Clear IPC queue
        proc->ipc_queue.head = 0;
        proc->ipc_queue.tail = 0;
        proc->ipc_queue.count = 0;

        // Clear process structure - this will set state to PROC_TERMINATED (0)
        uint32_t pid_backup = proc->pid; // Keep for logging
        FastMemset(proc, 0, sizeof(EEVDFProcessControlBlock));

        // Free the slot
        FreeSlotFast(slot);
        AtomicDec(&process_count);
        cleanup_count++;

        PrintKernel("EEVDF: Process PID ");
        PrintKernelInt(pid_backup);
        PrintKernel(" cleaned up successfully (state now PROC_TERMINATED=0)\n");
    }
}

void EEVDFCleanupTerminatedProcess(void) {
    // Cleanup is now lockless except for individual process locks
    EEVDFCleanupTerminatedProcessInternal();
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
    PrintKernel("-------------------------------------------------------------------------------\n");
    
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
    PrintKernel("-------------------------------------------------------------------------------\n");
}

void EEVDFGetProcessStats(uint32_t pid, uint32_t* cpu_time, uint32_t* wait_time, uint32_t* preemptions) {
    EEVDFProcessControlBlock* proc = EEVDFGetCurrentProcessByPID(pid);
    if (!proc) {
        if (cpu_time) *cpu_time = 0;
        if (wait_time) *wait_time = 0;
        if (preemptions) *preemptions = 0;
        return;
    }
    
    // Atomic reads for statistics
    if (cpu_time) *cpu_time = (uint32_t)AtomicRead64(&proc->cpu_time_accumulated);
    if (wait_time) *wait_time = (uint32_t)AtomicRead64(&proc->wait_sum);
    if (preemptions) *preemptions = AtomicRead(&proc->preemption_count);
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