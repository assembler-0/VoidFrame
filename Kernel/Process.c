#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
#include "Io.h"
static Process processes[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_process = 0;
static uint32_t process_count = 0;
static int need_schedule = 0;
extern void SwitchContext(ProcessContext * old, ProcessContext * new);
void ProcessInit(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = 0;
        processes[i].state = PROC_TERMINATED;
        processes[i].stack = 0;
    }
    
    // Create an idle process (PID 0)
    processes[0].pid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].priority = 0;

    for (int i = 0; i < sizeof(ProcessContext)/8; i++) {
        ((uint64_t*)&processes[0].context)[i] = 0;
    }

    process_count = 1;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    if (process_count >= MAX_PROCESSES) return 0;
    // Find free slot
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) return 0;

    // Allocate stack
    void* stack = AllocPage();
    if (!stack) return 0;

    // Initialize a process
    processes[slot].pid = next_pid++;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].priority = 1;

    // Clear all context registers
    for (int i = 0; i < sizeof(ProcessContext)/8; i++) {
        ((uint64_t*)&processes[slot].context)[i] = 0;
    }

    // Set up initial context - stack grows downward
    processes[slot].context.rsp = (uint64_t)stack + STACK_SIZE - 8;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202;
    
    process_count++;
    return processes[slot].pid;
}

void Schedule(void) {
    if (process_count <= 1) return;
    
    // Find next ready process
    uint32_t next = (current_process + 1) % MAX_PROCESSES;
    uint32_t start = next;
    
    do {
        if (processes[next].state == PROC_READY) {
            // Update current process state
            if (processes[current_process].state == PROC_RUNNING) {
                processes[current_process].state = PROC_READY;
            }
            
            // Update new process state
            processes[next].state = PROC_RUNNING;
            
            // Switch context if different process
            if (next != current_process) {
                ProcessContext* old_ctx = &processes[current_process].context;
                ProcessContext* new_ctx = &processes[next].context;
                current_process = next;
                SwitchContext(old_ctx, new_ctx);
            }
            return;
        }
        next = (next + 1) % MAX_PROCESSES;
    } while (next != start);
}


Process* GetCurrentProcess(void) {
    return &processes[current_process];
}