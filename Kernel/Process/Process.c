#include "Process.h"
#include "Kernel.h"
#include "Memory.h"
#include "Panic.h"
#include "Io.h"
#include "../Memory/MemOps.h"

#define NULL ((void*)0)
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

// Fast slot allocation using bitmap
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

void ProcessInit(void) {
    FastMemset(processes, 0, sizeof(Process) * MAX_PROCESSES);
    active_process_bitmap = 0;
    last_scheduled_slot = 0;

    // Create idle process (PID 0)
    processes[0].pid = 0;
    processes[0].state = PROC_RUNNING;
    processes[0].priority = 255;
    processes[0].privilege_level = PROC_PRIV_SYSTEM;
    processes[0].is_user_mode = 0;

    // Don't set bit 0 in bitmap - idle process is special
    init_token(&processes[0].token, 0, PROC_PRIV_SYSTEM, 0);

    process_count = 1;
    current_process = 0;
}

uint32_t CreateProcess(void (*entry_point)(void)) {
    return CreateSecureProcess(entry_point, PROC_PRIV_USER);
}

void ProcessExitStub() {
    PrintKernel("[KERNEL] Process returned from its main function. This is an error!\n");
    PrintKernel("Terminating process PID: ");
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
            PrintKernel("[SYSTEM] Denied: PID ");
            PrintKernelInt(creator->pid);
            PrintKernel(" attempted to create a system-level process.\n");
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
    return new_pid;
}

void ScheduleFromInterrupt(struct Registers* regs) {
    if (process_count <= 1) return;

    // Save current context
    FastMemcpy(&processes[current_process].context, regs, sizeof(struct Registers));
    if (processes[current_process].state == PROC_RUNNING) {
        processes[current_process].state = PROC_READY;
    }

    // Fast round-robin using bitmap
    uint32_t next_slot = current_process;
    uint32_t start_slot = (last_scheduled_slot + 1) % MAX_PROCESSES;

    // Look for next ready process starting from where we left off
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        uint32_t candidate = (start_slot + i) % MAX_PROCESSES;

        // Skip slot 0 unless it's our only option
        if (candidate == 0 && process_count > 1) continue;

        // Check if slot is active and process is ready
        if (candidate < 32 && (active_process_bitmap & (1U << candidate))) {
            if (processes[candidate].state == PROC_READY && processes[candidate].pid != 0) {
                next_slot = candidate;
                last_scheduled_slot = candidate;
                break;
            }
        }
    }

    // Fallback to current process if it's still ready
    if (next_slot == current_process && processes[current_process].state != PROC_READY) {
        next_slot = 0;  // Idle process
        last_scheduled_slot = 0;
    }

    // Switch process
    current_process = next_slot;
    processes[current_process].state = PROC_RUNNING;

    // Restore context
    FastMemcpy(regs, &processes[current_process].context, sizeof(struct Registers));
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

void SystemService(void) {
    while (1) {
        Yield();
    }
}
void SecureKernelIntegritySubsystem(void) {
    PrintKernel("[SYSTEM] SecureKernelIntegritySubsystem() initializing...\n");

    Process* current = GetCurrentProcess();
    RegisterSecurityManager(current->pid);

    PrintKernel("[SYSTEM] Creating system service...\n");
    uint32_t service_pid = CreateSecureProcess(SystemService, PROC_PRIV_SYSTEM);
    if (service_pid) {
        PrintKernel("[SYSTEM] System now under SecureKernelIntegritySubsystem() control.\n");
    } else {
        Panic("[SYSTEM] Failed to create system service.\n");
    }
    PrintKernel("[SYSTEM] SecureKernelIntegritySubsystem() deploying...");
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
