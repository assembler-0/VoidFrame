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

// Security globals
static uint32_t security_manager_pid = 0;  // PID of SecureKernelIntegritySubsystem
static uint64_t security_magic = 0x5EC0DE4D41474943ULL;  // "SECODEMAGIC"

extern void SwitchContext(ProcessContext * old, ProcessContext * new);

static inline uint16_t CalculateChecksum(const SecurityToken* token) {
    const uint8_t* data = (const uint8_t*)token;
    uint16_t sum = 0;
    // Skip checksum field itself (last 2 bytes)
    for (int i = 0; i < sizeof(SecurityToken) - 2; i++) {
        sum += data[i];
    }
    return sum;
}

static inline int ValidateToken(const SecurityToken* token) {
    return (token->magic == security_magic) &&
           (token->checksum == CalculateChecksum(token));
}

static void init_token(SecurityToken* token, uint32_t creator_pid, uint8_t privilege) {
    token->magic = security_magic;
    token->creator_pid = creator_pid;
    token->privilege = privilege;
    token->flags = 0;
    token->checksum = CalculateChecksum(token);
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

void ProcessInit(void) {
    // Clear all processes
    for (int i = 0; i < MAX_PROCESSES; i++) {
        processes[i].pid = 0;
        processes[i].state = PROC_TERMINATED;
        processes[i].stack = 0;
        processes[i].privilege_level = PROC_PRIV_RESTRICTED;
    }

    // Create idle process (PID 0) - system privilege
    processes[0].pid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].priority = 0;
    processes[0].privilege_level = PROC_PRIV_SYSTEM;
    processes[0].is_user_mode = 0;

    // Initialize security token for idle process
    init_token(&processes[0].token, 0, PROC_PRIV_SYSTEM);

    // Clear context
    for (int i = 0; i < sizeof(ProcessContext)/8; i++) {
        ((uint64_t*)&processes[0].context)[i] = 0;
    }

    process_count = 1;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER);
}

uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege) {
    if (!entry_point) {
        Panic("CreateSecureProcess: NULL entry point");
    }
    // Fast security check - only security manager can create system processes
    if (privilege == PROC_PRIV_SYSTEM) {
        if (current_process >= MAX_PROCESSES) {
            Panic("CreateSecureProcess: Invalid current process");
        }

        // Only security manager (or initial kernel) can create system processes
        if (security_manager_pid != 0 && processes[current_process].pid != security_manager_pid) {
            PrintKernel("[SECURITY] Unauthorized system process creation attempt\n");
            return 0; // Fail silently for security
        }
    }

    if (process_count >= MAX_PROCESSES) {
        Panic("CreateSecureProcess: Too many processes");
    }

    // Find free slot (optimized loop)
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (processes[i].state == PROC_TERMINATED) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        Panic("CreateSecureProcess: No free process slots");
    }

    // Allocate stack
    void* stack = AllocPage();
    if (!stack) {
        Panic("CreateSecureProcess: Failed to allocate stack");
    }

    // Initialize process
    processes[slot].pid = next_pid++;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].priority = (privilege == PROC_PRIV_SYSTEM) ? 0 : 1;
    processes[slot].is_user_mode = (privilege != PROC_PRIV_SYSTEM);
    processes[slot].privilege_level = privilege;

    // Initialize security token
    uint32_t creator_pid = (current_process < MAX_PROCESSES) ? processes[current_process].pid : 0;
    init_token(&processes[slot].token, creator_pid, privilege);

    // Clear context
    for (int i = 0; i < sizeof(ProcessContext)/8; i++) {
        ((uint64_t*)&processes[slot].context)[i] = 0;
    }

    // Set up initial context
    processes[slot].context.rsp = (uint64_t)stack + STACK_SIZE - 8;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202;

    process_count++;

    return processes[slot].pid;
}

void Schedule(void) {
    if (process_count <= 1) return;
    
    // Find next ready process, prioritizing system processes
    uint32_t next = (current_process + 1) % MAX_PROCESSES;
    uint32_t start = next;
    uint32_t best_candidate = current_process;
    uint8_t best_priority = 255;

    do {
        if (processes[next].state == PROC_READY || processes[next].state == PROC_RUNNING) {
            if (next != current_process) {
                // Validate security token
                if (ValidateToken(&processes[next].token)) {
                    // Priority: system processes (0) > user processes (1)
                    if (processes[next].priority < best_priority) {
                        best_candidate = next;
                        best_priority = processes[next].priority;
                    }
                } else {
                    // Invalid token - terminate process
                    PrintKernel("[SECURITY] Invalid token detected, terminating process\n");
                    processes[next].state = PROC_TERMINATED;
                    if (processes[next].stack) {
                        // FreePage(processes[next].stack); // Assuming you have this function
                    }
                    process_count--;
                }
            }
        }
        next = (next + 1) % MAX_PROCESSES;
    } while (next != start);

    // Switch to best candidate if different from current
    if (best_candidate != current_process) {
        // Update current process state
        if (processes[current_process].state == PROC_RUNNING) {
            processes[current_process].state = PROC_READY;
        }

        // Update new process state
        processes[best_candidate].state = PROC_RUNNING;

        // Context switch
        ProcessContext* old_ctx = &processes[current_process].context;
        ProcessContext* new_ctx = &processes[best_candidate].context;
        current_process = best_candidate;
        SwitchContext(old_ctx, new_ctx);
    }
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

void RegisterSecurityManager(uint32_t pid) {
    security_manager_pid = pid;
    PrintKernel("[SECURITY] Security manager registered with PID: ");
    PrintKernelInt(pid);
    PrintKernel("\n");
}

void SystemService(void) {
    PrintKernel("[SYSTEM] System service started\n");
    while (1) {
        // Do system work
        for (volatile int i = 0; i < 10000; i++);
    }
}

void SecureKernelIntegritySubsystem(void) {
    // Register this process as the security manager
    Process* current = GetCurrentProcess();
    if (current) {
        RegisterSecurityManager(current->pid);
        PrintKernel("[SECURITY] SecureKernelIntegritySubsystem active\n");
    }

    // Example: Create a system service process
    uint32_t service_pid = CreateSecureProcess(SystemService, PROC_PRIV_SYSTEM);
    if (service_pid) {
        PrintKernel("[SECURITY] Created system service with PID: ");
        PrintKernelInt(service_pid);
        PrintKernel("\n");
    }

    // Main security loop
    while (1) {
        // Periodic security checks
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (processes[i].state != PROC_TERMINATED) {
                // Validate process tokens
                if (!ValidateToken(&processes[i].token)) {
                    PrintKernel("[SECURITY] Token validation failed for PID: ");
                    PrintKernelInt(processes[i].pid);
                    PrintKernel("\n");
                    // Terminate compromised process
                    processes[i].state = PROC_TERMINATED;
                    process_count--;
                }
            }
        }

        // Small delay to prevent excessive CPU usage
        for (volatile int i = 0; i < 1000; i++);
    }
}

// Example system service that can only be created by security manager
