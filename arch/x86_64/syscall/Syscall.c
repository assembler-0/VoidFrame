#include "Syscall.h"
#include "Console.h"
#include "Gdt.h"
#include "Idt.h"
#include "Ipc.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "PS2.h"
#include "Panic.h"
#include "Pic.h"
#include "Process.h"
#include "StringOps.h"
#include "UserMode.h"
#include "VFS.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
extern void SyscallEntry(void);
uint64_t Syscall(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    Process* current = GetCurrentProcess();
    if (unlikely(!current)) {
        Panic("Syscall from invalid process");
    }
    switch (syscall_num) {
        case SYS_EXIT:

            if (current) {
                KillProcess(current->pid);
                RequestSchedule();
            }

            return 0;
            
        case SYS_WRITE:
            // arg1 = fd (ignored for now), arg2 = buffer, arg3 = count
            if (!is_user_address((const void*)arg2, arg3)) {
                return -1; // Bad address
            }
            if (likely(arg1 == 1)) { // stdout
                if (unlikely(!arg2)) {
                    return -1; // NULL buffer
                }
                // Limit the size to prevent buffer overflows in kernel space
                if (unlikely(arg3 > MAX_SYSCALL_BUFFER_SIZE)) {
                    return -1; // Buffer too large
                }

                // Copy user buffer to a kernel-controlled buffer
                char kernel_buffer[MAX_SYSCALL_BUFFER_SIZE + 1]; // +1 for null terminator
                FastMemcpy(kernel_buffer, (const void*)arg2, arg3);
                kernel_buffer[arg3] = '\0'; // Ensure null termination
                
                PrintKernel(kernel_buffer);
                return arg3;
            }
            return -1;
            
        case SYS_READ:
            // unix approach i think
            while (!HasInput()) {
                Yield(); // yield like a good citizen
            }
            return GetChar();
            
        case SYS_GETPID:
            return current ? current->pid : -1;

        case SYS_IPC_SEND:
            // arg1 = target_pid, arg2 = message
            IpcSendMessage((uint32_t)arg1, (IpcMessage*)arg2);
            return 0;

        case SYS_IPC_RECV:
            // arg1 = message_buffer
            return IpcReceiveMessage((IpcMessage*)arg1);

        case SYS_OPEN:
            return 0;

        case SYS_CLOSE:
            return 0;

        case SYS_FREAD:
        {
            // arg1 = path, arg2 = user buffer, arg3 = count
            char path_fread[MAX_SYSCALL_BUFFER_SIZE + 1];

            // Validate path pointer
            if (!is_user_address((const void*)arg1, FastStrlen((const char*)arg1, arg1))) return -1;
            FastStrCopy(path_fread, (const char*)arg1, arg1);

            // Validate the user's destination buffer pointer
            if (!is_user_address((void*)arg2, arg3)) return -1;

            // Read directly into the user's buffer.
            // stac has made this possible. VfsReadFile needs to be safe.
            int bytes_read = VfsReadFile(path_fread, (uint8_t*)arg2, arg3);
            return bytes_read;
        }

        case SYS_FWRITE:
            return 0;

        case SYS_MKDIR:
            (void)arg3;
            char mkdir_path[MAX_SYSCALL_BUFFER_SIZE + 1]; // +1 for null terminator
            FastMemcpy(mkdir_path, (const void*)arg1, arg2);
            mkdir_path[arg2] = '\0'; // Ensure null termination
            VfsCreateDir(mkdir_path);
            return 0;

        case SYS_STAT:
            (void)arg3;
            char ls_path[MAX_SYSCALL_BUFFER_SIZE + 1]; // +1 for null terminator
            FastMemcpy(ls_path, (const void*)arg1, arg2);
            ls_path[arg2] = '\0'; // Ensure null termination
            VfsListDir(ls_path);
            return 0;

        case SYS_RM:
            (void)arg3;
            char rm_path[MAX_SYSCALL_BUFFER_SIZE + 1]; // +1 for null terminator
            FastMemcpy(rm_path, (const void*)arg1, arg2);
            rm_path[arg2] = '\0'; // Ensure null termination
            VfsDelete(rm_path);
            return 0;

        case SYS_SET_FREQ:
            (void)arg2;
            (void)arg3;
            if (arg1 <= 0 || arg1 > 65535) return -1;
            PitSetFrequency((uint16_t)arg1);
            return 0;

        default:
            return -1;
    }
}

void SyscallInit(void) {
    // Install syscall interrupt (0x80)
    IdtSetGate(0x80, (uint64_t)SyscallEntry, KERNEL_CODE_SELECTOR, IDT_TRAP_GATE_USER);
}