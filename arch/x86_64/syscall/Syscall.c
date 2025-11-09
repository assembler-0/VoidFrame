#include <Syscall.h>
#include <Console.h>
#include <Scheduler.h>
#include <VFS.h>
#include <Ipc.h>
#include <MemOps.h>

#define MAX_FILE_DESCRIPTORS 256
#define MAX_SYSCALL_STR_LEN 256

typedef struct {
    bool in_use;
    char path[256];
    uint64_t position;
} FileHandle;

static FileHandle file_descriptor_table[MAX_FILE_DESCRIPTORS];

void InitializeSyscall() {
    for (int i = 0; i < MAX_FILE_DESCRIPTORS; i++) {
        file_descriptor_table[i].in_use = false;
    }
}

uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    char kernel_buffer[MAX_SYSCALL_BUFFER_SIZE];
    char path_buffer[MAX_SYSCALL_STR_LEN];

    switch (syscall_num) {
        case SYS_WRITE: {
            int fd = (int)arg1;
            const void* user_buffer = (const void*)arg2;
            uint32_t count = (uint32_t)arg3;

            if (fd == 1 || fd == 2) { // stdout or stderr
                if (count > MAX_SYSCALL_BUFFER_SIZE) {
                    count = MAX_SYSCALL_BUFFER_SIZE;
                }
                if (CopyFromUser(kernel_buffer, user_buffer, count) != 0) {
                    return -1;
                }
                if (fd == 1) {
                    PrintKernel(kernel_buffer);
                } else {
                    PrintKernelError(kernel_buffer);
                }
                return count;
            }

            if (fd >= 3 && fd < MAX_FILE_DESCRIPTORS && file_descriptor_table[fd].in_use) {
                if (count > MAX_SYSCALL_BUFFER_SIZE) {
                    count = MAX_SYSCALL_BUFFER_SIZE;
                }
                if (CopyFromUser(kernel_buffer, user_buffer, count) != 0) {
                    return -1;
                }
                int bytes_written = VfsWriteAt(file_descriptor_table[fd].path, kernel_buffer, file_descriptor_table[fd].position, count);
                if (bytes_written > 0) {
                    file_descriptor_table[fd].position += bytes_written;
                }
                return bytes_written;
            }
            return -1;
        }

        case SYS_EXIT:
            KillCurrentProcess("SYS_EXIT");
            Yield();
            return arg1;

        case SYS_READ: {
            int fd = (int)arg1;
            void* user_buffer = (void*)arg2;
            uint32_t count = (uint32_t)arg3;

            if (fd >= 3 && fd < MAX_FILE_DESCRIPTORS && file_descriptor_table[fd].in_use) {
                if (count > MAX_SYSCALL_BUFFER_SIZE) {
                    count = MAX_SYSCALL_BUFFER_SIZE;
                }
                int bytes_read = VfsReadAt(file_descriptor_table[fd].path, kernel_buffer, file_descriptor_table[fd].position, count);
                if (bytes_read > 0) {
                    if (CopyToUser(user_buffer, kernel_buffer, bytes_read) != 0) {
                        return -1;
                    }
                    file_descriptor_table[fd].position += bytes_read;
                }
                return bytes_read;
            }
            return -1;
        }

        case SYS_OPEN: {
            const char* user_path = (const char*)arg1;
            if (CopyFromUser(path_buffer, user_path, MAX_SYSCALL_STR_LEN) != 0) {
                return -1;
            }
            path_buffer[MAX_SYSCALL_STR_LEN - 1] = '\0';

            for (int i = 3; i < MAX_FILE_DESCRIPTORS; i++) {
                if (!file_descriptor_table[i].in_use) {
                    file_descriptor_table[i].in_use = true;
                    strncpy(file_descriptor_table[i].path, path_buffer, sizeof(file_descriptor_table[i].path) - 1);
                    file_descriptor_table[i].position = 0;
                    return i;
                }
            }
            return -1; // No available file descriptors
        }

        case SYS_CLOSE: {
            int fd = (int)arg1;
            if (fd >= 3 && fd < MAX_FILE_DESCRIPTORS && file_descriptor_table[fd].in_use) {
                file_descriptor_table[fd].in_use = false;
                return 0;
            }
            return -1; // Invalid file descriptor
        }

        case SYS_CREATE_FILE: {
            const char* user_path = (const char*)arg1;
            if (CopyFromUser(path_buffer, user_path, MAX_SYSCALL_STR_LEN) != 0) {
                return -1;
            }
            path_buffer[MAX_SYSCALL_STR_LEN - 1] = '\0';
            return VfsCreateFile(path_buffer);
        }

        case SYS_CREATE_DIR: {
            const char* user_path = (const char*)arg1;
            if (CopyFromUser(path_buffer, user_path, MAX_SYSCALL_STR_LEN) != 0) {
                return -1;
            }
            path_buffer[MAX_SYSCALL_STR_LEN - 1] = '\0';
            return VfsCreateDir(path_buffer);
        }

        case SYS_DELETE: {
            const char* user_path = (const char*)arg1;
            if (CopyFromUser(path_buffer, user_path, MAX_SYSCALL_STR_LEN) != 0) {
                return -1;
            }
            path_buffer[MAX_SYSCALL_STR_LEN - 1] = '\0';
            return VfsDelete(path_buffer, false); // Not recursive by default
        }

        case SYS_LIST_DIR: {
            const char* user_path = (const char*)arg1;
            if (CopyFromUser(path_buffer, user_path, MAX_SYSCALL_STR_LEN) != 0) {
                return -1;
            }
            path_buffer[MAX_SYSCALL_STR_LEN - 1] = '\0';
            return VfsListDir(path_buffer);
        }

        case SYS_CREATE_PROCESS: {
            const char* user_name = (const char*)arg1;
            void (*entry_point)() = (void (*)())arg2;
            if (CopyFromUser(path_buffer, user_name, MAX_SYSCALL_STR_LEN) != 0) {
                return -1;
            }
            path_buffer[MAX_SYSCALL_STR_LEN - 1] = '\0';
            return CreateProcess(path_buffer, entry_point);
        }

        case SYS_KILL_PROCESS: {
            uint32_t pid = (uint32_t)arg1;
            KillProcess(pid);
            return 0;
        }

        case SYS_GET_PID: {
            CurrentProcessControlBlock* pcb = GetCurrentProcess();
            if (pcb) {
                return pcb->pid;
            }
            return -1;
        }

        case SYS_YIELD: {
            Yield();
            return 0;
        }

        case SYS_IPC_SEND_MESSAGE: {
            uint32_t target_pid = (uint32_t)arg1;
            const IpcMessage* user_msg = (const IpcMessage*)arg2;
            IpcMessage kernel_msg;
            if (CopyFromUser(&kernel_msg, user_msg, sizeof(IpcMessage)) != 0) {
                return -1;
            }
            return IpcSendMessage(target_pid, &kernel_msg);
        }

        case SYS_IPC_RECEIVE_MESSAGE: {
            IpcMessage* user_msg_buffer = (IpcMessage*)arg1;
            IpcMessage kernel_msg;
            IpcResult result = IpcReceiveMessage(&kernel_msg);
            if (result == IPC_SUCCESS) {
                if (CopyToUser(user_msg_buffer, &kernel_msg, sizeof(IpcMessage)) != 0) {
                    return -1;
                }
            }
            return result;
        }

        default:
            return -1;
    }
}
