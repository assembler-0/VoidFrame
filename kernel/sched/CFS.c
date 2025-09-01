
#include "CFS.h"
#include "MLFQ.h"
#include "stdbool.h"

// =============================================================================
// CFS Data Structures
// =============================================================================

// 1. Red-Black Tree Node
// Each sched in the run queue will have one of these.
// The tree will be ordered by vruntime.

typedef struct RbNode {
    struct RbNode* parent;
    struct RbNode* left;
    struct RbNode* right;
    uint64_t vruntime;       // The key for sorting
    uint32_t slot;           // The slot of the sched in the main 'processes' array
    bool is_red;
} RbNode;

// 2. CFS Scheduler State
// This holds the root of the RB-Tree and other CFS-specific state.
typedef struct {
    RbNode* run_queue_root;
    uint64_t min_vruntime;   // The minimum vruntime in the tree
    uint32_t num_running;    // Number of processes in the run queue
} CfsScheduler;


// =============================================================================
// Private CFS Functions (Placeholders)
// =============================================================================

static void CfsEnqueue(uint32_t slot) {
    // TODO: Implement RB-Tree insertion logic here
}

static void CfsDequeue(uint32_t slot) {
    // TODO: Implement RB-Tree removal logic here
}

static uint32_t CfsPickNext() {
    // TODO: Find the leftmost node in the RB-Tree
    return 0; // Return idle sched if queue is empty
}

static void CfsTick(uint32_t current_slot) {
    // TODO: Update vruntime for the running sched
}


// =============================================================================
// Public Interface
// =============================================================================

void CfsInit(struct SchedulerAPI* api) {
    // This function will be called to set up the CFS scheduler.
    // It populates the provided API struct with its own functions.

    // api->init = CfsInit; // Or maybe a no-op for re-init
    // api->add_process = CfsEnqueue;
    // api->remove_process = CfsDequeue;
    // api->next_process = CfsPickNext;
    // api->tick = CfsTick;
}
