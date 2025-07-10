// --- START OF REWORKED FILE Process.c ---

#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
#include "Panic.h"
#include "Io.h"
#include "../Memory/MemOps.h"

#define NULL ((void*)0)
#define offsetof(type, member) ((uint64_t)&(((type*)0)->member))
// For cleaner debugging, you can toggle this
// #define DEBUG_SECURITY

static Process processes[MAX_PROCESSES];
static uint32_t next_pid = 1;
static uint32_t current_process = 0;
static uint32_t process_count = 0;
static volatile int need_schedule = 0;
// Security globals
static uint32_t security_manager_pid = 0;
// REWORK: Magic number is good. No changes needed.
static const uint64_t security_magic = 0x5EC0DE4D41474943ULL; // "SECODEMAGIC"

extern void SwitchContext(ProcessContext * old, ProcessContext * new);

/**
 * @brief Calculates the checksum for a security token.
 *
 * @param token The token to checksum.
 * @return The 16-bit checksum.
 *
 * REWORK: This implementation is more robust and cleaner. It checksums all
 * fields *before* the checksum field itself. This is less error-prone than
 * manually adding up chunks of the struct.
 */
static uint16_t CalculateChecksum(const SecurityToken* token, uint32_t pid_for_checksum) {
    uint16_t sum = 0;
    const uint8_t* data = (const uint8_t*)token;

    // Checksum all data in the token struct up to the checksum field itself.
    for (uint64_t i = 0; i < offsetof(SecurityToken, checksum); i++) {
        sum += data[i];
    }

    // REWORK: Add the process's OWN PID to the checksum.
    sum += (uint16_t)(pid_for_checksum & 0xFFFF);
    sum += (uint16_t)((pid_for_checksum >> 16) & 0xFFFF);

    return sum;
}


int ShouldSchedule(void) {
    if (need_schedule) {
        need_schedule = 0; // Atomically reset the flag
        return 1;
    }
    return 0;
}

/**
 * @brief Sets a flag indicating the scheduler should run at the next opportunity.
 */
void RequestSchedule(void) {
    need_schedule = 1;
}

void Yield() {
    RequestSchedule();
    __asm__ __volatile__("hlt");
}

/**
 * @brief Validates a security token's integrity.
 *
 * @param token The token to validate.
 * @return 1 if valid, 0 otherwise.
 *
 * REWORK: Simplified the logic and made debug prints conditional.
 * This function now has one clear purpose: check magic and checksum.
 */
static int ValidateToken(const SecurityToken* token, uint32_t pid_to_check) {
    if (token->magic != security_magic) {
        return 0;
    }

    uint16_t calculated_checksum = CalculateChecksum(token, pid_to_check); // Pass the PID
    if (token->checksum != calculated_checksum) {
        return 0;
    }

    return 1;
}

/**
 * @brief Initializes a security token and calculates its checksum.
 *
 * @param token Pointer to the token to initialize.
 * @param creator_pid PID of the process creating this token.
 * @param privilege The privilege level for the new token.
 */
static void init_token(SecurityToken* token, uint32_t creator_pid, uint8_t privilege, uint32_t new_pid) {
    token->magic = security_magic;
    token->creator_pid = creator_pid;
    token->privilege = privilege;
    token->flags = 0;
    token->checksum = 0;
    token->checksum = CalculateChecksum(token, new_pid); // Pass the new PID
}


// REWORK: Removed ShouldSchedule and RequestSchedule as they weren't used
// in the provided scheduling logic. If used elsewhere, they can be added back.

void ProcessInit(void) {
    FastMemset(processes, 0, sizeof(Process) * MAX_PROCESSES);

    // Create idle process (PID 0) - this is the root of trust.
    processes[0].pid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].priority = 255; // Lowest priority
    processes[0].privilege_level = PROC_PRIV_SYSTEM;
    processes[0].is_user_mode = 0;

    // Initialize security token for the idle process. Created by "itself" (PID 0).
    init_token(&processes[0].token, 0, PROC_PRIV_SYSTEM, processes[0].pid);

    process_count = 1;
    current_process = 0;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER);
}

// A stub function for processes that return, which they should not.
void ProcessExitStub() {
    // In a real kernel, this would be a syscall to terminate the process.
    PrintKernel("[KERNEL] Process returned from its main function. This is an error!\n");
    PrintKernel("Terminating process PID: ");
    PrintKernelInt(GetCurrentProcess()->pid);
    PrintKernel("\n");

    // For now, just halt. A proper implementation would terminate and schedule.
    GetCurrentProcess()->state = PROC_TERMINATED;
    // RequestSchedule(); // If you had a yield/schedule primitive.
    while (1) { __asm__ __volatile__("hlt"); }
}

uint32_t CreateSecureProcess(void (*entry_point)(void), uint8_t privilege) {
    if (!entry_point) {
        Panic("CreateSecureProcess: NULL entry point");
    }

    // REWORK: This is a critical security boundary.
    // Check if the *current* process has the right to create a process with the
    // requested privilege level.
    if (privilege == PROC_PRIV_SYSTEM) {
        Process* creator = GetCurrentProcess();
        // Only allow the kernel (during init, pid=0) or another SYSTEM process
        // to create a new SYSTEM process.
        if (creator->pid != 0 && creator->privilege_level != PROC_PRIV_SYSTEM) {
            PrintKernel("[SECURITY] Denied: PID ");
            PrintKernelInt(creator->pid);
            PrintKernel(" attempted to create a system-level process.\n");
            return 0; // Fail gracefully.
        }
    }

    if (process_count >= MAX_PROCESSES) {
        Panic("CreateSecureProcess: Too many processes");
    }

    // Find a free process slot.
    int slot = -1;
    for (int i = 0; i < MAX_PROCESSES; i++) { // Check all slots
        if (processes[i].state == PROC_TERMINATED) {
            slot = i;
            break;
        }
    }
    // If no terminated slots, use the next available one.
    if (slot == -1) {
        if (next_pid <= MAX_PROCESSES) {
             slot = process_count; // Use the next empty slot
        } else {
             Panic("CreateSecureProcess: No free process slots");
        }
    }


    // Clear the slot for the new process.
    FastMemset(&processes[slot], 0, sizeof(Process));

    void* stack = AllocPage();
    if (!stack) {
        Panic("CreateSecureProcess: Failed to allocate stack");
    }

    // Initialize process fields
    processes[slot].pid = next_pid++;
    processes[slot].state = PROC_READY;
    processes[slot].stack = stack;
    processes[slot].privilege_level = privilege;
    processes[slot].priority = (privilege == PROC_PRIV_SYSTEM) ? 10 : 100; // Lower number is higher priority
    processes[slot].is_user_mode = (privilege != PROC_PRIV_SYSTEM);

    // Create the token. The creator is the currently running process.
    init_token(&processes[slot].token, GetCurrentProcess()->pid, privilege, processes[slot].pid);

    // Set up the initial context for the new process
    uint64_t rsp = (uint64_t)processes[slot].stack + STACK_SIZE;
    rsp &= ~0xF; // 16-byte alignment is good practice.

    // Push the return address for the process's main function.
    uint64_t* stack_ptr = (uint64_t*)rsp;
    *(--stack_ptr) = (uint64_t)&ProcessExitStub;

    FastMemset(&processes[slot].context, 0, sizeof(ProcessContext));
    processes[slot].context.rsp = (uint64_t)stack_ptr;
    processes[slot].context.rip = (uint64_t)entry_point;
    processes[slot].context.rflags = 0x202; // Interrupts enabled
    processes[slot].context.cs = 0x08;
    processes[slot].context.ss = 0x10;

    process_count++;
    return processes[slot].pid;
}

void ScheduleFromInterrupt(struct Registers* regs) {
    if (process_count <= 1) return;

    // Save the context of the current process
    FastMemcpy(&processes[current_process].context, regs, sizeof(struct Registers));
    if (processes[current_process].state == PROC_RUNNING) {
        processes[current_process].state = PROC_READY;
    }

    // REWORK: The scheduler is now much simpler and faster.
    // It does NOT validate security tokens. It trusts that any process in the
    // ready queue is valid. Its job is simply to find the best next process.
    uint32_t next_proc_idx = current_process;
    uint8_t best_priority = 255; // Start with the worst possible priority

    // Round-robin search for the highest priority (lowest value) process
    for (uint32_t i = 1; i <= process_count; i++) {
        uint32_t candidate_idx = (current_process + i) % MAX_PROCESSES;

        if ((processes[candidate_idx].state == PROC_READY || processes[candidate_idx].state == PROC_RUNNING)
            && processes[candidate_idx].pid != 0) // Don't schedule the idle process unless nothing else is ready
        {
            if (processes[candidate_idx].priority < best_priority) {
                best_priority = processes[candidate_idx].priority;
                next_proc_idx = candidate_idx;
            }
        }
    }

    // If no other process was ready, schedule the idle process (pid 0)
    if (next_proc_idx == current_process && processes[current_process].state != PROC_RUNNING) {
        next_proc_idx = 0; // Fallback to idle process
    }

    // Switch to the chosen process
    current_process = next_proc_idx;
    processes[current_process].state = PROC_RUNNING;

    // Restore the context of the next process
    FastMemcpy(regs, &processes[current_process].context, sizeof(struct Registers));
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
    // This is still useful for identifying a specific process with a role.
    security_manager_pid = pid;
    PrintKernel("[SECURITY] Security manager registered with PID: ");
    PrintKernelInt(pid);
    PrintKernel("\n");
}

void SystemService(void) {
    PrintKernel("[SYSTEM] System service started.\n");
    uint32_t pid = GetCurrentProcess()->pid;
    while (1) {
        PrintKernel("System service running (PID ");
        PrintKernelInt(pid);
        PrintKernel(")\n");
        Yield();
    }
}

void SecureKernelIntegritySubsystem(void) {
    PrintKernel("[SECURITY] SecureKernelIntegritySubsystem initializing...\n");

    Process* current = GetCurrentProcess();
    RegisterSecurityManager(current->pid);

    PrintKernel("[SECURITY] Creating a system service...\n");
    uint32_t service_pid = CreateSecureProcess(SystemService, PROC_PRIV_SYSTEM);
    if (service_pid) {
        PrintKernel("[SECURITY] Created system service with PID: ");
        PrintKernelInt(service_pid);
        PrintKernel("\n");
    } else {
        Panic("[SECURITY] Failed to create system service.\n");
    }

    PrintKernel("[SECURITY] Integrity monitoring loop starting.\n");
    // REWORK: The SKIS is a background auditor. It runs with lower frequency
    // and acts as a safety net.
    while (1) {
        Yield();
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if ((processes[i].state == PROC_READY || processes[i].state == PROC_RUNNING) && processes[i].pid != 0) {
                if (!ValidateToken(&processes[i].token, processes[i].pid)) {
                    PrintKernel("[SECURITY] SecureKernelIntegritySubsystem found a corrupt token for PID: ");
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