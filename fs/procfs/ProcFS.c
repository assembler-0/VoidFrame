#include "ProcFS.h"
#include "VFS.h"
#include "KernelHeap.h"
#include "StringOps.h"
#include "Console.h"
#include "Format.h"
#include "stdlib.h"
#include "MemOps.h"
#include "Scheduler.h"

static ProcFSEntry* proc_list_head = NULL;

void ProcFSInit() {
    proc_list_head = NULL;
    PrintKernelF("VFS: Initialized ProcFS\n");
}

void ProcFSRegisterProcess(uint32_t pid, void* data) {
    ProcFSEntry* new_entry = (ProcFSEntry*)KernelMemoryAlloc(sizeof(ProcFSEntry));
    if (!new_entry) {
        PrintKernelError("VFS: Failed to allocate memory for new process entry\n");
        return;
    }
    new_entry->pid = pid;
    new_entry->next = proc_list_head;
    proc_list_head = new_entry;
}

void ProcFSUnregisterProcess(uint32_t pid) {
    ProcFSEntry* current = proc_list_head;
    ProcFSEntry* prev = NULL;

    while (current) {
        if (current->pid == pid) {
            if (prev) {
                prev->next = current->next;
            } else {
                proc_list_head = current->next;
            }
            KernelFree(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

int ProcfsMount(struct BlockDevice* device, const char* mount_point) {
    (void)device;
    (void)mount_point;
    return 0;
}

int ProcfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (path[0] != '/') return -1;

    char pid_str[16];
    int i = 1;
    int j = 0;
    while (path[i] != '/' && path[i] != '\0' && j < 15) {
        pid_str[j++] = path[i++];
    }
    pid_str[j] = '\0';

    if (path[i] == '\0') return -1;

    uint32_t pid = atoi(pid_str);
    CurrentProcessControlBlock* pcb = GetCurrentProcessByPID(pid);

    if (!pcb) {
        return -1;
    }

    const char* filename = &path[i + 1];
    char local_buffer[1024];

    if (FastStrCmp(filename, "info") == 0) {
        int len = FormatA(local_buffer, sizeof(local_buffer),
                         "Name: %s\n"
                         "PID: %u\n"
                         "State: %d\n"
                         "PPID: %u\n"
                         "Priority: %d\n"
                         "Privilege: %d\n"
                         "CPU Time: %llu ticks\n"
                         "Creation Time: %llu\n",
                         pcb->name,
                         pcb->pid,
                         pcb->state,
                         pcb->parent_pid,
#if defined(VF_CONFIG_SCHED_MLFQ)
                         pcb->priority,
#else
                         pcb->nice,
#endif
                         pcb->privilege_level,
                         pcb->cpu_time_accumulated,
                         pcb->creation_time);

        if (len > max_size) len = max_size;
        FastMemcpy(buffer, local_buffer, len);
        return len;
    }
    return -1;
}

int ProcfsWriteFile(const char* path, const void* buffer, uint32_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    return -1;
}

int ProcfsListDir(const char* path) {
    if (FastStrCmp(path, "/") == 0) {
        ProcFSEntry* current = proc_list_head;
        while (current) {
            PrintKernelF("  %d/\n", current->pid);
            current = current->next;
        }
        return 0;
    }

    if (path[0] == '/' && path[1] != '\0') {
        char pid_str[16];
        int i = 1;
        int j = 0;
        while (path[i] != '/' && path[i] != '\0' && j < 15) {
            pid_str[j++] = path[i++];
        }
        pid_str[j] = '\0';

        uint32_t pid = atoi(pid_str);
        ProcFSEntry* current = proc_list_head;
        while (current) {
            if (current->pid == pid) {
                if (path[i] == '\0' || (path[i] == '/' && path[i+1] == '\0')) {
                    PrintKernelF("  info\n");
                    return 0;
                }
            }
            current = current->next;
        }
    }
    return -1;
}

int ProcfsIsDir(const char* path) {
    if (FastStrCmp(path, "/") == 0) {
        return 1;
    }

    if (path[0] == '/' && path[1] != '\0') {
        char pid_str[16];
        int i = 1;
        int j = 0;
        while (path[i] != '/' && path[i] != '\0' && j < 15) {
            pid_str[j++] = path[i++];
        }
        pid_str[j] = '\0';

        uint32_t pid = atoi(pid_str);
        ProcFSEntry* current = proc_list_head;
        while (current) {
            if (current->pid == pid) {
                if (path[i] == '\0' || (path[i] == '/' && path[i+1] == '\0')) {
                    return 1;
                }
            }
            current = current->next;
        }
    }
    return 0;
}