#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
#include "Panic.h"
#include "Io.h"
static Process processes[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_process = 0;
static uint32_t process_count = 0;
static int need_schedule = 0;
extern void SwitchContext(ProcessContext * old, ProcessContext * new);

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
    if (!entry_point) {
        Panic("CreateProcess: NULL entry point");
    }
    
    if (process_count >= MAX_PROCESSES) {
        Panic("CreateProcess: Too many processes");
    }
    
    // Find free slot
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        Panic("CreateProcess: No free process slots");
    }

    // Allocate stack
    void* stack = AllocPage();
    if (!stack) {
        Panic("CreateProcess: Failed to allocate stack");
    }

    // Initialize a process
    processes[slot].pid = next_pid++;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].priority = 1;
    processes[slot].is_user_mode = 0;

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
        if (processes[next].state == PROC_READY || processes[next].state == PROC_RUNNING) {
            // Don't switch to the same process
            if (next == current_process) {
                next = (next + 1) % MAX_PROCESSES;
                if (next == start) break;
                continue;
            }
            
            // Update current process state to READY if it was RUNNING
            if (processes[current_process].state == PROC_RUNNING) {
                processes[current_process].state = PROC_READY;
            }
            
            // Update new process state to RUNNING
            processes[next].state = PROC_RUNNING;
            // Switch context
            ProcessContext* old_ctx = &processes[current_process].context;
            ProcessContext* new_ctx = &processes[next].context;
            current_process = next;
            SwitchContext(old_ctx, new_ctx);
            return;
        }
        next = (next + 1) % MAX_PROCESSES;
    } while (next != start);
}


void ScheduleFromInterrupt(struct Registers* regs) {
    if (!regs) {
        Panic("ScheduleFromInterrupt: NULL registers");
    }
    
    if (current_process >= MAX_PROCESSES) {
        Panic("ScheduleFromInterrupt: Invalid current process");
    }
    
    if (process_count <= 1) return;
    
    // Find next ready process
    uint32_t next = (current_process + 1) % MAX_PROCESSES;
    uint32_t start = next;
    
    do {
        if (processes[next].state == PROC_READY || processes[next].state == PROC_RUNNING) {
            if (next == current_process) {
                next = (next + 1) % MAX_PROCESSES;
                if (next == start) break;
                continue;
            }
            
            // Save current process context from interrupt frame
            processes[current_process].context.rax = regs->rax;
            processes[current_process].context.rbx = regs->rbx;
            processes[current_process].context.rcx = regs->rcx;
            processes[current_process].context.rdx = regs->rdx;
            processes[current_process].context.rsi = regs->rsi;
            processes[current_process].context.rdi = regs->rdi;
            processes[current_process].context.rbp = regs->rbp;
            processes[current_process].context.rsp = regs->rsp;
            processes[current_process].context.r8 = regs->r8;
            processes[current_process].context.r9 = regs->r9;
            processes[current_process].context.r10 = regs->r10;
            processes[current_process].context.r11 = regs->r11;
            processes[current_process].context.r12 = regs->r12;
            processes[current_process].context.r13 = regs->r13;
            processes[current_process].context.r14 = regs->r14;
            processes[current_process].context.r15 = regs->r15;
            processes[current_process].context.rip = regs->rip;
            processes[current_process].context.rflags = regs->rflags;
            
            // Update states
            if (processes[current_process].state == PROC_RUNNING) {
                processes[current_process].state = PROC_READY;
            }
            processes[next].state = PROC_RUNNING;

            // Load new process context into interrupt frame
            regs->rax = processes[next].context.rax;
            regs->rbx = processes[next].context.rbx;
            regs->rcx = processes[next].context.rcx;
            regs->rdx = processes[next].context.rdx;
            regs->rsi = processes[next].context.rsi;
            regs->rdi = processes[next].context.rdi;
            regs->rbp = processes[next].context.rbp;
            regs->rsp = processes[next].context.rsp;
            regs->r8 = processes[next].context.r8;
            regs->r9 = processes[next].context.r9;
            regs->r10 = processes[next].context.r10;
            regs->r11 = processes[next].context.r11;
            regs->r12 = processes[next].context.r12;
            regs->r13 = processes[next].context.r13;
            regs->r14 = processes[next].context.r14;
            regs->r15 = processes[next].context.r15;
            regs->rip = processes[next].context.rip;
            regs->rflags = processes[next].context.rflags;
            
            current_process = next;
            return;
        }
        next = (next + 1) % MAX_PROCESSES;
    } while (next != start);
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
    return 0;
}