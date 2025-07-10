#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
#include "Panic.h"
#include "Io.h"
#include "../Memory/MemOps.h" // Use kernel memory ops instead of <string.h>
#define NULL ((void*)0)
static Process processes[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_process = 0;
static uint32_t process_count = 0;
static int need_schedule = 0;

// Security globals
static uint32_t security_manager_pid = 0;  // PID of SecureKernelIntegritySubsystem
static uint64_t security_magic = 0x5EC0DE4D41474943ULL;  // "SECODEMAGIC"

extern void SwitchContext(ProcessContext * old, ProcessContext * new);

// Debug function to check PID 0's token magic
void CheckPid0Magic(const char* location) {
    PrintKernel("CheckPid0Magic at ");
    PrintKernel(location);
    PrintKernel(": ");
    PrintKernelHex(processes[0].token.magic);
    PrintKernel("\n");
}

static inline uint16_t CalculateChecksum(const SecurityToken* token) {
    uint16_t sum = 0;

    // Sum magic (8 bytes)
    sum += (uint16_t)(token->magic & 0xFFFF);
    sum += (uint16_t)((token->magic >> 16) & 0xFFFF);
    sum += (uint16_t)((token->magic >> 32) & 0xFFFF);
    sum += (uint16_t)((token->magic >> 48) & 0xFFFF);

    // Sum creator_pid (4 bytes)
    sum += (uint16_t)(token->creator_pid & 0xFFFF);
    sum += (uint16_t)((token->creator_pid >> 16) & 0xFFFF);

    // Sum privilege (1 byte)
    sum += (uint16_t)token->privilege;

    // Sum flags (1 byte)
    sum += (uint16_t)token->flags;

    return sum;
}

static inline int ValidateToken(const SecurityToken* token) {
    uint16_t calculated_checksum = CalculateChecksum(token);
    int is_valid = (token->magic == security_magic) &&
                   (token->checksum == calculated_checksum);

    PrintKernel("ValidateToken Debug for PID: ");
    PrintKernelInt(token->creator_pid); // Using creator_pid as a proxy for process ID
    PrintKernel("\n");
    PrintKernel("  Token Magic: "); PrintKernelHex(token->magic); PrintKernel("\n");
    PrintKernel("  Expected Magic: "); PrintKernelHex(security_magic); PrintKernel("\n");
    PrintKernel("  Token Checksum: "); PrintKernelHex(token->checksum); PrintKernel("\n");
    PrintKernel("  Calculated Checksum: "); PrintKernelHex(calculated_checksum); PrintKernel("\n");
    PrintKernel("  Magic Match: "); PrintKernelInt(token->magic == security_magic); PrintKernel("\n");
    PrintKernel("  Checksum Match: "); PrintKernelInt(token->checksum == calculated_checksum); PrintKernel("\n");
    PrintKernel("  Is Valid: "); PrintKernelInt(is_valid); PrintKernel("\n");

    return is_valid;
}

static void init_token(SecurityToken* token, uint32_t creator_pid, uint8_t privilege) {
    token->magic = security_magic;
    token->creator_pid = creator_pid;
    token->privilege = privilege;
    token->flags = 0;
    token->checksum = 0; // Zero out checksum before calculating
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
    // Clear all processes array
    FastMemset(processes, 0, sizeof(Process) * MAX_PROCESSES);

    // Debug print for SecurityToken size
    PrintKernel("sizeof(SecurityToken): ");
    PrintKernelInt(sizeof(SecurityToken));
    PrintKernel("\n");

    // Create idle process (PID 0) - system privilege
    processes[0].pid = 0;


    processes[0].state = PROC_RUNNING;
    processes[0].priority = 0;
    processes[0].privilege_level = PROC_PRIV_SYSTEM;
    processes[0].is_user_mode = 0;

    // Initialize security token for idle process
    init_token(&processes[0].token, 0, PROC_PRIV_SYSTEM);
    CheckPid0Magic("After init_token in ProcessInit");

    // Clear context
    for (int i = 0; i < sizeof(ProcessContext)/8; i++) {
        ((uint64_t*)&processes[0].context)[i] = 0;
    }

    process_count = 1;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER);
}

void SecureProcessExitStub() {
    PrintKernel("[SECURITY] Process returned! This shouldn't happen!\n");
    while (1) { __asm__ __volatile__("hlt"); }
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

    // Clear the entire process slot to ensure a clean state
    FastMemset(&processes[slot], 0, sizeof(Process));

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
    FastMemset(&processes[slot].context, 0, sizeof(ProcessContext));

    uint64_t rsp = (uint64_t)processes[slot].stack + STACK_SIZE;
    rsp &= ~0xF; // 16-byte alignment

    // Set up stack with proper return address
    uint64_t* stack_ptr = (uint64_t*)rsp;
    *(--stack_ptr) = (uint64_t)&SecureProcessExitStub;

    // Initialize the context as a proper interrupt frame
    processes[slot].context.rsp = (uint64_t)stack_ptr;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202; // Interrupts enabled, bit 1 always 1
    processes[slot].context.cs = 0x08;  // Kernel code segment
    processes[slot].context.ss = 0x10;  // Kernel data segment
    processes[slot].context.ds = 0x10;  // Kernel data segment
    processes[slot].context.es = 0x10;  // Kernel data segment
    processes[slot].context.fs = 0x10;  // Kernel data segment
    processes[slot].context.gs = 0x10;  // Kernel data segment
    processes[slot].context.interrupt_number = 0;
    processes[slot].context.error_code = 0;

    process_count++;
    return processes[slot].pid;
}

void ScheduleFromInterrupt(struct Registers* regs) {
    CheckPid0Magic("Start of ScheduleFromInterrupt");
    if (!regs) {
        Panic("ScheduleFromInterrupt: NULL registers");
    }
    
    if (current_process >= MAX_PROCESSES) {
        Panic("ScheduleFromInterrupt: Invalid current process");
    }
    
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
                        FreePage(processes[next].stack);
                        processes[next].stack = NULL;
                        }
                    process_count--;
                }
            }
        }
        next = (next + 1) % MAX_PROCESSES;
    } while (next != start);

    // Switch to best candidate if different from current
    if (best_candidate != current_process) {
        // Save current process's full interrupt frame
        // Now this is safe because ProcessContext == struct Registers
        FastMemcpy(&processes[current_process].context, regs, sizeof(struct Registers));

        // Update current process state
        if (processes[current_process].state == PROC_RUNNING) {
            processes[current_process].state = PROC_READY;
        }

        // Update new process state
        processes[best_candidate].state = PROC_RUNNING;

        // Restore next process's full interrupt frame
        FastMemcpy(regs, &processes[best_candidate].context, sizeof(struct Registers));
        current_process = best_candidate;
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
    PrintKernel("SecureKernelIntegritySubsystem() Initializing...\n");
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
        CheckPid0Magic("Start of SecureKernelIntegritySubsystem loop");
        static int check_counter = 0;
        if (++check_counter % 1000 != 0) {
            for (volatile int i = 0; i < 1000; i++);
            continue;
        }
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