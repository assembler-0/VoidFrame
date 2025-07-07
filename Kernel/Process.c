#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
static Process processes[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_process = 0;
static uint32_t process_count = 0;
static int need_schedule = 0;
extern void SwitchContext(ProcessContext * old, ProcessContext * new);
int ShouldSchedule(void) {
    if (need_schedule) {
        need_schedule = 0;  // RESET the flag!
        PrintKernel("Schedulling now\n");
        return 1;
    }
    return 0;
}
void RequestSchedule(void) {
    need_schedule = 1;
    PrintKernel("Schedule requested\n");
}
void ProcessInit(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = 0;
        processes[i].state = PROC_TERMINATED;
        processes[i].stack = 0;
    }
    
    // Create idle process (PID 0)
    processes[0].pid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].priority = 0;

    // asm volatile(
    //     "mov %%rsp, %0\n"
    //     "pushfq\n"
    //     "pop %1\n"
    //     : "=m"(processes[0].context.rsp), "=m"(processes[0].context.rflags)
    //     :
    //     : "memory"
    // );
    //
    // processes[0].context.rip = 0;

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

    // Initialize process
    processes[slot].pid = next_pid++;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].priority = 1;

    // Clear all context registers
    for (int i = 0; i < sizeof(ProcessContext)/8; i++) {
        ((uint64_t*)&processes[slot].context)[i] = 0;
    }

    // Set up initial context
    processes[slot].context.rsp = (uint64_t)stack + STACK_SIZE - 16;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202;
    
    process_count++;
    return processes[slot].pid;
}

void Schedule(void) {
    if (process_count <= 1) return;
    uint32_t next = (current_process + 1) % MAX_PROCESSES;

    while (next != current_process) {
        if (processes[next].state == PROC_READY) {
            // Found a process to switch to!
            ProcessContext* old_ctx = &processes[current_process].context;
            ProcessContext* new_ctx = &processes[next].context;

            // Update states
            if (processes[current_process].state == PROC_RUNNING) {
                processes[current_process].state = PROC_READY;
            }
            processes[next].state = PROC_RUNNING;
            current_process = next;

            PrintKernel("Switched to process ");
            PrintKernelInt(current_process);
            PrintKernel("\n");
            SwitchContext(old_ctx, new_ctx);
            break;
        }
        next = (next + 1) % MAX_PROCESSES;
    }
}

Process* GetCurrentProcess(void) {
    return &processes[current_process];
}