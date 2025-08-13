#pragma once
#include "stdint.h"

// VFS Mount Points
#define VFS_MAX_MOUNTS 8

typedef enum {
    VFS_RAMFS = 0,
    VFS_FAT12 = 1
} VfsType;

typedef struct {
    char mount_point[64];
    VfsType type;
    uint8_t drive;  // For disk-based FS
    int active;
} VfsMountStruct;

// VFS Functions
int VfsInit(void);
int VfsMount(const char* path, VfsType type, uint8_t drive);
int VfsReadFile(const char* path, void* buffer, uint32_t max_size);
int VfsWriteFile(const char* path, const void* buffer, uint32_t size);
int VfsListDir(const char* path);
int VfsCreateFile(const char* path);
int VfsCreateDir(const char* path);
int VfsDelete(const char* path);
int VfsIsDir(const char* path);
uint64_t VfsGetFileSize(const char* path);
// Internal
VfsMountStruct* VfsFindMount(const char* path);
const char* VfsStripMount(const char* path, VfsMountStruct* mount);