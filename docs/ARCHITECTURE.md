# The VoidFrame monolithic kernel 💫

## Table of Contents

- [System Requirements](#system-requirements)
- [Architecture](#architecture)
- [Boot Process](#boot-process)
- [Memory Layout](#memory-layout)
- [Process Management](#process-management)
- [Debugging and Development](#debugging-and-development)

### Key Features
- [here!](ROADMAP.md)
### System Requirements
- Hardware requirements
  - AVX2/SSE2 support
  - 64-bit support
  - PS/2 keyboard and mouse (recommended)
  - 16MB of RAM (minimum floor, 1% memory fragmentation as tested)
  - 128MiB of RAM (or more) (recommended)
  - FAT12/16-formatted IDE drive (optional)
  - xHCI controller (optional)
  - RTL8139 (optional)
  - SB16 (optional)
- Supported architecture
  - x86_64
  - aarch64 (soon?)

## Architecture

### Core Components
#### Assembly Bootstrap (PXS0):
- **Purpose**: Switch from protected mode to long mode and initialize the stack for the kernel
- **Location**: `arch/x86_64/asm/pxs.asm`
- **Key files**: `pxs.asm`
- **Note**: If the kernel is run with `-debugcon stdio`, string: `1KCSWF23Z456789` will be expected at stdio, any missing characters will identify a major issue.

#### C-level Bootstrap: (PXS1 & PXS2)
- **Purpose**: Initialize core kernel subsystems and jump to the higher half
- **Location**: `kernel/core`
- **Key files**: `Kernel.c`

## Boot Process

### Boot Sequence Overview
1. **Bootloader Phase**
    - GRUB loads /boot/voidframe.krnl @ 0x100000 (for obvious reason(s))
    - Request framebuffer and VESA (800x600)

2. **Assembly bootstrap**
    - File: `arch/x86_64/asm/pxs.asm`
    - Store magic and info in eax and ebx (1)
    - Setup stack (K)
    - Check and enable features, extensions (FCS)
    - Load GDT (2)
    - Enable PAE (3)
    - Setup and zero page tables (Z)
    - Set CR3 (4)
    - Map 4G with 2MB pages (5)
    - Enable Long mode (6)
    - Enable Paging (7)
    - Entered 64-bit mode (8)
    - Setup stack & Jump to kernel entry point (9)
    - Extra: (R!) Kernel returns.

3. **PXS1**
    - Entry function: `KernelMain()` & `PXS1()`
    - Check for magic and info
    - Set fallback VGA mode buffer 0xB8000 
    - Start Serial driver across all ports
    - Start VESA driver
    - Start console driver
    - Parse MB2 info
    - Start PMM (Physical Memory Manager)
    - Create new PML4
    - Identity-map kernel sections
    - Jump to Virtual half

4. **PXS2**
    - Entry function: `KernelMainHigherHalf()` & `PXS2()`
    - Check CPU features
    - Validate memory layout
    - Start VMM
    - Start Kernel HEAP
    - Start Memory pools
    - Initialize GDT & IDT
    - Install PIC and PIT
    - Start PS/2 Drivers
    - Start Shell
    - Start IDE driver
    - Start ramfs Driver (VFRFS)
    - Start VFS driver
    - Start Process management (and core subsystems)
    - Check huge page support
    - Start & detect ISA bus device(s)
    - Enumerate PCI device(s)
    - Start xHCI controller
    - Start RTL8139 driver
    - Start LPT driver
    - Remap IRQs
    - Initrd for RAMFS
    - Start stack guard 
    - Setup memory protections
    - Enable interrupts

## Memory Layout
1. **Load**
   - The kernel is loaded at physical address `0x100000` (1MB). 
2. **PMM**
   - Physical Memory Manager will find the highest physical address using the multiboot2 info.
   - It will then create a bitmap to manage physical memory.
   - It will reserve the first 1MB of memory for the kernel and other critical structures like the multiboot2 info itself.
3. **Kernel Sections**
    - The kernel is divided into several sections: `.text`, `.rodata`, `.data`, `.bss`, and the heap.
    - Each section is aligned to page boundaries (4KB).
    - The `.bss` section is zero-initialized.
    - The heap starts immediately after the `.bss` section and grows upwards.
4. **Preparing for the jump**
    - The kernel prepares to jump to the higher half by setting up page tables that map the kernel's physical addresses to virtual addresses starting at `0xFFFFFFFF80000000`.
    - This involves creating a new PML4 and setting up the necessary entries in the page tables.
5. **Higher Half**
    - After the jump, the kernel operates in the higher half of the address space.
6. **VMM**
    - Virtual Memory Manager (VMM) will get PML4 from the higher half (or more specifically, from CR3 that bootstrap set).
    - It will track kernel space and test identity mapping. A faulty test will result in a kernel panic.
7. **Heap**
    - The kernel heap is initialized using class-based memory pools.
    - It is also started with validation_level = 1 (aka `KHEAP_VALIDATION_BASIC`, though, it could be changed at will).  
8. **Layout**
    - The memory layout is as follows:
      ```
      +---------------------+ 0xFFFFFFFFFFFFFFFFULL
      |                     |
      |                     |
      |    Kernel Space     |
      |                     |
      |                     |
      +---------------------+ 0xFFFFFE0000000000ULL
      |                     |
      |                     |
      |                     |
      |                     |
      |                     |
      |    Kernel Heap      |
      |                     |
      |                     |
      |                     |
      |                     |
      |                     |
      +---------------------+ 0xFFFF800000000000ULL
      ```
      Quite simple, isn't it?

## Process Management

### 1. Overview

1.  **High-Performance Scheduling:** A sophisticated Multi-Level Feedback Queue (MLFQ) scheduler ensures that the system is both responsive for interactive tasks and efficient for long-running, CPU-intensive workloads.
2.  **Proactive Security:** A dedicated security process, **Astra**, constantly monitors system integrity, validating processes and responding to threats in real-time.
3.  **Dynamic Performance Scaling:** A smart controller, **DynamoX**, adaptively adjusts the system's core frequency based on current load to optimize performance and power usage.

### 2. The Process Control Block (PCB)

The `ProcessControlBlock` is the fundamental data structure that represents a process in the kernel. It stores all essential information required to manage and schedule the process.

**File:** `Process.h`

```c
typedef struct {
    uint32_t pid;
    ProcessState state;
    void* stack;
    uint8_t priority;
    uint8_t base_priority;   
    uint8_t is_user_mode;
    uint8_t privilege_level;
    uint32_t cpu_burst_history[CPU_BURST_HISTORY];
    uint32_t io_operations;   
    uint32_t preemption_count;
    uint64_t cpu_time_accumulated;
    uint64_t last_scheduled_tick;
    uint64_t wait_time;        
    TerminationReason term_reason;
    uint32_t exit_code;
    uint64_t termination_time;
    uint32_t parent_pid;
    SecurityToken token;
    MessageQueue ipc_queue;
    ProcessContext context;
    SchedulerNode* scheduler_node;
    uint64_t creation_time;
    char* ProcINFOPath;
} ProcessControlBlock;
```

#### Key PCB Fields

| Field                  | Description                                                                                                                |
|:-----------------------|:---------------------------------------------------------------------------------------------------------------------------|
| `pid`                  | A unique Process Identifier. PID `0` is reserved for the idle process.                                                     |
| `state`                | The current execution state of the process (e.g., Running, Ready, Blocked).                                                |
| `stack`                | A pointer to the process's allocated kernel stack.                                                                         |
| `context`              | Stores the CPU register state (`rip`, `rsp`, etc.) when the process is not running. This is crucial for context switching. |
| `priority`             | The process's current priority level in the scheduler. This value can change dynamically.                                  |
| `base_priority`        | The initial priority assigned at creation, used to reset a process's priority.                                             |
| `privilege_level`      | The security privilege level (`PROC_PRIV_SYSTEM` or `PROC_PRIV_USER`).                                                     |
| `token`                | A `SecurityToken` structure used by the Astra security system to verify the process's integrity.                           |
| `scheduler_node`       | A pointer to the node that links this PCB into the scheduler's priority queues.                                            |
| `cpu_time_accumulated` | Total system ticks this process has spent running on the CPU.                                                              |
| `io_operations`        | A counter for I/O-related blocking events, used to identify I/O-bound processes.                                           |

### 3. Process Lifecycle & States

A process transitions through several states from its creation to its termination.

**File:** `Process.h`

*   **`PROC_READY`**: The process is ready to run and is waiting in a scheduler queue for its turn on the CPU.
*   **`PROC_RUNNING`**: The process is currently executing on the CPU.
*   **`PROC_BLOCKED`**: The process is waiting for an event (e.g., I/O completion) and cannot proceed until that event occurs.
*   **`PROC_DYING`**: The process has been signaled for termination and is in the process of being shut down. It is removed from active scheduling.
*   **`PROC_ZOMBIE`**: The process has terminated, but its PCB is kept in a termination queue to be cleaned up. It has released most of its resources.
*   **`PROC_TERMINATED`**: The state `0`. The process slot is free, and all its resources have been reclaimed.

### 4. Process Creation and Termination

#### Process Creation (`CreateSecureProcess`)

Process creation is handled by the `CreateSecureProcess` function, which is designed with security as a primary concern.

**File:** `Process.c`

The creation workflow is as follows:
1.  **Security Validation:** It validates the security token of the *creator* process. A process with a corrupt token cannot create new processes.
2.  **Privilege Check:** It prevents user-level processes from creating system-level processes. Such an attempt is considered a hostile act and results in the termination of the caller by **Astra**.
3.  **Resource Allocation:**
   *   Finds a free process slot and allocates a unique PID.
   *   Allocates a kernel stack for the new process.
4.  **PCB Initialization:**
   *   The `ProcessControlBlock` is securely zeroed and initialized with default values.
   *   An initial priority is assigned based on its privilege level.
5.  **Security Token Generation:** A new `SecurityToken` is created for the process. It contains the creator's PID, its own flags, and a secure checksum (`CalculateSecureChecksum`) to prevent tampering.
6.  **Context Setup:** The initial CPU context is configured. The instruction pointer (`rip`) is set to the process's entry point, and the stack pointer (`rsp`) is prepared with the `ProcessExitStub` as the final return address, ensuring clean termination.
7.  **Scheduling:** The process state is set to `PROC_READY` and it is added to the scheduler via `AddToScheduler`.

#### Process Termination (`TerminateProcess` & Cleanup)

Termination is a robust, multi-stage process designed to be safe and secure.

**File:** `Process.c`

1.  **Initiation (`TerminateProcess`):**
   *   A termination request is received with a `TerminationReason` (e.g., `TERM_NORMAL`, `TERM_KILLED`, `TERM_SECURITY`).
   *   **Security Checks:** If one process tries to kill another, the system verifies that the caller has the necessary privileges. User processes cannot kill system processes, and critical/immune processes are protected. The caller's security token is also validated.
   *   **State Transition:** The process state is atomically changed to `PROC_DYING`. This prevents race conditions.
   *   **De-scheduling:** The process is immediately removed from the scheduler's queues (`RemoveFromScheduler`).
   *   **Cleanup Queuing:** The process state is set to `PROC_ZOMBIE`, and its slot index is added to a lockless termination queue.

2.  **Resource Reclamation (`CleanupTerminatedProcesses`):**
   *   This function runs periodically as part of the `DynamoX` system process.
   *   It safely dequeues `PROC_ZOMBIE` processes from the termination queue.
   *   For each zombie, it frees its final resources (like the stack), clears the `ProcessControlBlock`, and frees the process slot and PID for future use.

### 5. The MLFQ Scheduler (`FastSchedule`)

The kernel employs a Multi-Level Feedback Queue (MLFQ) scheduler, a highly adaptive algorithm designed to achieve low latency for interactive tasks and high throughput for batch tasks.

**File:** `Process.c`, `Process.h`

#### Architecture

*   **Priority Levels:** The scheduler uses `MAX_PRIORITY_LEVELS` (5) queues. The levels are divided into two categories:
   *   **Real-Time (Levels 0 to `RT_PRIORITY_THRESHOLD-1`):** These queues have absolute priority. A process in level 0 will always run before a process in level 1. They are intended for critical system tasks and highly responsive user applications.
   *   **Regular (Levels `RT_PRIORITY_THRESHOLD` to 4):** These are for normal user and system processes.
*   **Quantum:** Each priority level is assigned a time slice, or "quantum." Higher-priority queues have smaller quantums for faster response times, while lower-priority queues have larger quantums for better CPU throughput.

#### Scheduling Algorithm (`FastSchedule`)

The `FastSchedule` function is invoked by the timer interrupt. Its logic is as follows:
1.  **Find Next Process:** It scans the priority queues from highest to lowest to find the first non-empty queue (`FindBestQueue`). The process at the head of this queue is the best candidate to run next.
2.  **Preemption Logic:** The currently running process can be preempted (stopped) if:
   *   Its time quantum expires.
   *   A process appears in a significantly higher-priority queue. The scheduler has a `PREEMPTION_BIAS` to prevent excessive "thrashing" between processes of similar priority.
3.  **Context Switch:** If a new process is chosen, the CPU state of the old process is saved to its PCB, and the state of the new process is loaded from its PCB. This is a context switch.
4.  **Dynamic Priority Adjustment:** This is the "feedback" mechanism.
   *   **Demotion:** If a process consistently uses its entire time quantum, it is considered CPU-bound. The scheduler will lower its priority, moving it to a queue with a larger quantum, preventing it from hogging the CPU and starving interactive tasks.
   *   **Promotion:** If a process frequently blocks for I/O before its quantum expires (`ProcessBlocked`), it is identified as I/O-bound or interactive. The scheduler boosts its priority to a high-level queue to ensure it gets to run quickly when its I/O completes, leading to a snappier user experience.
5.  **Aging and Starvation Prevention (`SmartAging`):** To prevent processes in low-priority queues from never running (starvation), the `SmartAging` algorithm periodically scans the queues. If a process has been waiting for too long (`AGING_THRESHOLD_BASE`), its priority is temporarily boosted to a much higher level, giving it a chance to run.

### 6. Security Subsystem: Astra

Astra is a dedicated, high-privilege system process that functions as a real-time security manager. Its primary mission is to ensure the integrity of the kernel and all running processes.

**File:** `Process.c`

#### Security Tokens

Every process has a `SecurityToken` that acts as a digital signature.
*   It contains the process's flags, privilege level, and a `magic` number.
*   Crucially, it includes a `checksum` calculated from the token's contents and the process's PID.
*   The `ValidateToken` function can be called at any time to recalculate the checksum and compare it with the stored value. A mismatch indicates that the process's in-memory state has been corrupted or tampered with.

#### Threat Detection & Response

Astra runs in a continuous loop, performing a series of checks:
1.  **Token Integrity Scan:** Periodically iterates through active processes and validates their security tokens. A failed validation results in immediate termination of the suspicious process (`SecurityViolationHandler`).
2.  **Unauthorized Privilege Detection:** This is Astra's most critical check. It looks for processes that are running with `PROC_PRIV_SYSTEM` but do *not* have the `PROC_FLAG_SUPERVISOR` or `PROC_FLAG_CRITICAL` flags in their token. This indicates a process may have illicitly escalated its own privileges, and it is terminated with extreme prejudice via `ASTerminate`.
3.  **Scheduler Consistency Checks:** Astra cross-references the scheduler's process bitmap (`active_process_bitmap`) with the global `process_count` to detect desynchronization, which could be a sign of kernel data structure corruption.
4.  **Threat Level Management:** Astra maintains a system-wide `threat_level`. Each security violation increases this level.
   *   If the level exceeds a high threshold (`threat_level > 40`), Astra triggers **DEFCON 2**: a system-wide lockdown where all non-critical, non-immune processes are terminated to contain a potential widespread compromise.
   *   If the level becomes critical (`threat_level > 75`), Astra assumes the system is unrecoverable and triggers a kernel panic.

#### Pre-flight Checks (`AstraPreflightCheck`)

Before the scheduler context-switches to a process, it performs a final, lightweight security check. This check validates the process's token and verifies its privilege flags one last time, ensuring that a compromised process is never allowed to run on the CPU.

### 7. Dynamic Performance Controller: DynamoX

DynamoX is a system process that intelligently manages the CPU's operational frequency (simulated via the PIT timer) to deliver performance when needed and conserve power when the system is idle.

**File:** `Process.c`

#### How It Works

DynamoX runs in a loop, periodically sampling various system metrics:
*   **Process Load:** The number of active and ready processes.
*   **Queue Pressure:** The depth of the scheduler's priority queues. Deep queues indicate high demand for CPU time.
*   **Context Switch Rate:** A high rate of context switches can indicate system thrashing under heavy load.

Based on these metrics, it calculates a `target_freq`.
*   **Boosting:** If the load is high, queue pressure is mounting, or context switching is rapid, DynamoX increases the frequency towards `max_freq`. It includes an "emergency boost" for sudden, extreme spikes in load.
*   **Reducing:** If the system is idle (low process count, low context switch rate), it gradually lowers the frequency towards `min_freq`.
*   **Adaptive Learning:** DynamoX uses momentum and a learning rate to smooth out frequency changes, preventing erratic fluctuations. It also maintains different power states (e.g., `Balanced`, `Performance`, `Power Saving`) to make more intelligent, long-term decisions.

The goal is to create a system that feels fast and responsive under load but doesn't waste energy when idle.

### 8. Core APIs and Commands

The following functions and commands provide interfaces to the process management subsystem.

| Function / Command           | Description                                                                                                                      |
|:-----------------------------|:---------------------------------------------------------------------------------------------------------------------------------|
| `CreateProcess(entry_point)` | Creates a new user-level process. A simplified wrapper around `CreateSecureProcess`.                                             |
| `KillProcess(pid)`           | Safely requests the termination of a process with the given PID.                                                                 |
| `Yield()`                    | A cooperative function that allows a process to voluntarily give up the remainder of its time slice and trigger the scheduler.   |
| `GetProcessByPid(pid)`       | Retrieves a pointer to the PCB for a given PID.                                                                                  |
| `ListProcesses()`            | A shell/debug command that prints a formatted list of all active processes, including their PID, state, priority, and CPU usage. |
| `DumpSchedulerState()`       | A debug command that prints detailed internal statistics about the scheduler, including queue depths and load.                   |
| `DumpPerformanceStats()`     | A debug command that prints performance counters like total context switches and security violations.                            |

## Debugging and Development
- Debugging:
  - The VoidFrame kenrel offers multiple debugging options:
    - Serial output (COM1, COM2, COM3, COM4)
    - LPT ports
    - VGA text mode fallback
    - VESA framebuffer console
    - GDB stub (soon?)
- Development:
    - To develop the kernel, you *will* need the following tools:
      - clang (any kind, does not matter if its compiled for bare metal or for the kernel)
      - meson (i was thinking of using Makefiles and the auto tools stack but the concept of having objects files everywhere is not attractive)
      - qemu (you obviously need this)
      - gdb (optional, I dont even know to hook it up)
      - nasm (for the bootloader and many other low-level things)
      - grub-mkrescue + its dependencies (for building the iso, since the current kernel implementation is not bootable yet)
      - mkfs & qemu-img (or anything that can make disk image)
      - ld (preferably from gnu ld (gcc or whatever), dont know why but lld is giving me issues)
- Known issues:
  - The kernel is not *bootable* yet, its not an EFI stub, nor does it have a bootloader.
  - It is not a raw binary either, it needs to be loaded by a elf-compatible bootloader (in this case grub, but I am thinking of using a custom one, or just use grub for god’s sake).
  - It can only support up to 4GiB of memory. (it would still *technically* work but again, you would only see 4GiB of memory in the kernel)
  - The kernel is not multi-threaded, it is single-threaded.
  - The fault handler is mid, any fault with bring down the system.
  - It only works on qemu, for some reason??. ive tried Bochs, but it hardly ever gets to the shell, VBox is even worse.
  - The kernel's real hardware support is unknown, since I dont have a legacy BIOS pc :(.
- Parameters:
  - The kernel build options and flags are as follow (modify as needed)
      ```jetbrainsmeson
      vf_config_flags =  [
          '-DVF_CONFIG_ENABLE_XHCI',             # Enable xHCI driver
          '-DVF_CONFIG_VM_HOST',                 # Enable support VM host (aka still run with failing features chekcs)
          '-DVF_CONFIG_PROCINFO_CREATE_DEFAULT', # Create /Runtime/Processes/<pid> by default
          '-DVF_CONFIG_USE_VFSHELL',             # Enable VFShell
          '-DVF_CONFIG_USE_DYNAMOX',             # Enable DynamoX
          '-DVF_CONFIG_USE_ASTRA',               # Enable Astra
          '-DVF_CONFIG_USE_CERBERUS',            # Enable Cerberus (memory protection)
          '-DVF_CONFIG_PANIC_OVERRIDE',          # Ignore many panics (sometimes it gets quite annoying)
      ]
      ```
    Quite simple, isn't it?
> assembler-0 @ voidframe-kernel - 7:05 31/08/2025