#ifndef VOIDFRAME_PROCFS_H
#define VOIDFRAME_PROCFS_H

#include <BlockDevice.h>
#include <stdint.h>
#include <stdbool.h>

// Represents an entry in the procfs, typically a process.
// The data pointer is generic and can be used to store scheduler-specific info.
typedef struct ProcFSEntry {
    uint32_t pid; // Process ID
    struct ProcFSEntry* next; // Pointer to the next process entry
} ProcFSEntry;

// Initializes the procfs filesystem.
void ProcFSInit();

// Registers a new process with the given PID and associated data.
void ProcFSRegisterProcess(uint32_t pid, void* data);

// Unregisters a process from procfs using its PID.
void ProcFSUnregisterProcess(uint32_t pid);

// Mounts the procfs. This is a dummy function as procfs is a virtual filesystem.
int ProcfsMount(struct BlockDevice* device, const char* mount_point);

// Reads the content of a file within the procfs.
int ProcfsReadFile(const char* path, void* buffer, uint32_t max_size);

// Writes content to a file in procfs. Currently not supported.
int ProcfsWriteFile(const char* path, const void* buffer, uint32_t size);

// Lists the directory contents in procfs.
int ProcfsListDir(const char* path);

// Checks if a given path in procfs is a directory.
int ProcfsIsDir(const char* path);

#endif //VOIDFRAME_PROCFS_H