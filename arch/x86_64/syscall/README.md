# VoidFrame Syscall Interface

This document provides an overview of the VoidFrame syscall interface for the x86_64 architecture.

## Syscall Convention

Syscalls are invoked using the `int 80` (or `int 0x50`) instruction. The syscall number is passed in the `rax` register, and arguments are passed in `rdi`, `rsi`, `rdx`, `r10`, `r8`, and `r9`. The return value is placed in `rax`.

## Syscall Table

| Number | Name                      | arg1 (rdi)                 | arg2 (rsi)                | arg3 (rdx)         | Description                                      |
|--------|---------------------------|----------------------------|---------------------------|--------------------|--------------------------------------------------|
| 0      | `SYS_READ`                | `fd` (int)                 | `buffer` (void*)          | `count` (uint32_t) | Read from a file descriptor.                     |
| 1      | `SYS_WRITE`               | `fd` (int)                 | `buffer` (const void*)    | `count` (uint32_t) | Write to a file descriptor. (1=stdout, 2=stderr) |
| 2      | `SYS_OPEN`                | `path` (const char*)       | `flags` (int)             | -                  | Open a file and return a file descriptor.        |
| 3      | `SYS_CLOSE`               | `fd` (int)                 | -                         | -                  | Close a file descriptor.                         |
| 4      | `SYS_CREATE_FILE`         | `path` (const char*)       | -                         | -                  | Create a new file.                               |
| 5      | `SYS_CREATE_DIR`          | `path` (const char*)       | -                         | -                  | Create a new directory.                          |
| 6      | `SYS_DELETE`              | `path` (const char*)       | -                         | -                  | Delete a file or directory.                      |
| 7      | `SYS_LIST_DIR`            | `path` (const char*)       | -                         | -                  | List the contents of a directory.                |
| 8      | `SYS_CREATE_PROCESS`      | `name` (const char*)       | `entry_point` (void(*)()) | -                  | Create a new process.                            |
| 9      | `SYS_KILL_PROCESS`        | `pid` (uint32_t)           | -                         | -                  | Terminate a process by its ID.                   |
| 10     | `SYS_GET_PID`             | -                          | -                         | -                  | Get the process ID of the current process.       |
| 11     | `SYS_YIELD`               | -                          | -                         | -                  | Yield the CPU to another process.                |
| 12     | `SYS_IPC_SEND_MESSAGE`    | `target_pid` (uint32_t)    | `msg` (const IpcMessage*) | -                  | Send a message to a process.                     |
| 13     | `SYS_IPC_RECEIVE_MESSAGE` | `msg_buffer` (IpcMessage*) | -                         | -                  | Receive a message from the process's queue.      |
| 60     | `SYS_EXIT`                | `exit_code` (int)          | -                         | -                  | Terminate the current process.                   |

## Sample Assembly Code

The following is a simple example of how to use the syscall interface in x86_64 assembly (NASM syntax).

## Sample C Code with Inline Assembly

The following is a simple example of how to use the syscall interface in C with inline assembly.

```c
#include <stdint.h>

// Define syscall numbers (these would typically be in a header file)
#define SYS_WRITE           1
#define SYS_CREATE_FILE     4
#define SYS_EXIT            60

// Function to perform a syscall
static inline int64_t syscall3(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    int64_t ret;
    asm volatile (
        "movq %1, %%rax\n"  // syscall_num to rax
        "movq %2, %%rdi\n"  // arg1 to rdi
        "movq %3, %%rsi\n"  // arg2 to rsi
        "movq %4, %%rdx\n"  // arg3 to rdx
        "int $0x50\n"       // Invoke syscall
        : "=a" (ret)
        : "r" (syscall_num), "r" (arg1), "r" (arg2), "r" (arg3)
        : "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );
    return ret;
}

// Syscall with 2 arguments
static inline int64_t syscall2(uint64_t syscall_num, uint64_t arg1, uint64_t arg2) {
    return syscall3(syscall_num, arg1, arg2, 0); // Pass 0 for unused arg3
}

// Syscall with 1 argument
static inline int64_t syscall1(uint64_t syscall_num, uint64_t arg1) {
    return syscall3(syscall_num, arg1, 0, 0); // Pass 0 for unused arg2, arg3
}

// Syscall with 0 arguments
static inline int64_t syscall0(uint64_t syscall_num) {
    return syscall3(syscall_num, 0, 0, 0); // Pass 0 for unused args
}


void _start() {
    const char* message = "Hello from syscall!\n";
    const char* file_path = "/Data/test.txt";

    // Write "Hello from syscall!" to stdout
    syscall3(SYS_WRITE, 1, (uint64_t)message, (uint64_t)20); // 20 is length of "Hello from syscall!\n"

    // Create a file named "/Data/test.txt"
    syscall1(SYS_CREATE_FILE, (uint64_t)file_path);

    // Exit the process
    syscall1(SYS_EXIT, 0); // exit_code = 0
}
```