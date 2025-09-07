#include "VFS.h"
#include "../mm/MemOps.h"
#include "Console.h"
#include "FAT/FAT1x.h"
#include "EXT/Ext2.h"
#include "Format.h"
#include "KernelHeap.h"
#include "Serial.h"
#include "StringOps.h"
#include "VFRFS.h"
#include "stdbool.h"
#include "../kernel/atomic/Spinlock.h"
#include "../kernel/sched/MLFQ.h"

#define VFS_MAX_PATH_LEN 256

static VfsMountStruct mounts[VFS_MAX_MOUNTS];
static rwlock_t vfs_lock = RWLOCK_INIT;
int IsVFSInitialized = 0;

int VfsMount(const char* path, VfsType type, uint8_t drive) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            FastStrCopy(mounts[i].mount_point, path, 64);
            mounts[i].type = type;
            mounts[i].drive = drive;
            mounts[i].active = 1;

            // Create mount point directory in root filesystem (except for root mount)
            if (FastStrCmp(path, "/") != 0) {
                // Find root mount to create directory
                for (int j = 0; j < VFS_MAX_MOUNTS; j++) {
                    if (mounts[j].active && FastStrCmp(mounts[j].mount_point, "/") == 0) {
                        if (mounts[j].type == VFS_RAMFS) {
                            FsMkdir(path);
                        }
                        break;
                    }
                }
            }

            return 0;
        }
    }
    return -1;
}

int VfsInit(void) {
    PrintKernel("[VFS] Initializing Virtual File System...\n");

    // Clear mount table
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = 0;
    }
    SerialWrite("[VFS] Mount table cleared\n");

    // Mount RamFS as root
    SerialWrite("[VFS] Mounting RamFS as root...\n");
    int result = VfsMount("/", VFS_RAMFS, 0);
    if (result != 0) {
        SerialWrite("[VFS] Failed to mount root\n");
    }
    SerialWrite("[VFS] Root mounted\n");

    // Prioritize EXT2 mount first
    extern int ext2_initialized;
    if (ext2_initialized) {
        PrintKernel("[VFS] Attempting EXT2 mount...\n");
        int disk_result = VfsMount(FormatS("%s/VFSystemDrive", DevicesStorage), VFS_EXT2, 0);

        if (disk_result == 0) {
            SerialWriteF("[VFS] EXT2 mounted at %s/VFSystemDrive\n", DevicesStorage);
        } else {
            SerialWrite("[VFS] EXT2 mount failed\n");
        }
    } else {
        PrintKernel("[VFS] Skipping EXT2 mount - Not initialized\n");
    }

    extern int fat12_initialized;
    if (fat12_initialized) {
        PrintKernel("[VFS] Attempting FAT12 mount...\n");
        int disk_result = VfsMount(FormatS("%s/VFSystemDrive", DevicesStorage), VFS_FAT12, 0);

        if (disk_result == 0) {
            SerialWriteF("[VFS] FAT12 mounted at %s/VFSystemDrive\n", DevicesStorage);
        } else {
            SerialWrite("[VFS] FAT12 mount failed\n");
        }
    } else {
        PrintKernel("[VFS] Skipping FAT12 mount - Not initialized\n");
    }

    PrintKernelSuccess("[VFS] Virtual File System initialized\n");
    IsVFSInitialized = 1;
    return 0;
}

int VfsAppendFile(const char* path, const void* buffer, uint32_t size) {
    // Get file size first
    uint32_t current_size = VfsGetFileSize(path);
    if (current_size == 0) {
        // File doesn't exist or is empty, just write
        return VfsWriteFile(path, buffer, size);
    }

    // For now, just overwrite (proper append would need filesystem support)
    return VfsWriteFile(path, buffer, size);
}

VfsMountStruct* VfsFindMount(const char* path) {
    int best_match = -1;
    int best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) continue;

        int mount_len = FastStrlen(mounts[i].mount_point, 64);
        int path_len = FastStrlen(path, 256);

        if (path_len >= mount_len) {
            int matches = 1;
            for (int j = 0; j < mount_len; j++) {
                if (path[j] != mounts[i].mount_point[j]) {
                    matches = 0;
                    break;
                }
            }

            if (matches && (mount_len == 1 || path[mount_len] == '/' || path[mount_len] == '\0')) {
                if (mount_len > best_len) {
                    best_match = i;
                    best_len = mount_len;
                }
            }
        }
    }

    return (best_match >= 0) ? &mounts[best_match] : NULL;
}

const char* VfsStripMount(const char* path, VfsMountStruct* mount) {
    if (!path || !mount) return NULL;

    int mount_len = FastStrlen(mount->mount_point, 64);

    // If the mount is the root filesystem ("/"), the local path is the original path.
    if (mount_len == 1 && mount->mount_point[0] == '/') {
        return path;
    }

    const char* local_path_start = path + mount_len;

    if (*local_path_start == '\0') {
        return "/";
    }

    if (*local_path_start != '/') {
        return path; // Return original path as fallback
    }

    return local_path_start;
}

int VfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (!path || !buffer || max_size == 0) {
        SerialWrite("[VFS] Invalid parameters\n");
        return -1;
    }

    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) {
        SerialWrite("[VFS] No mount found\n");
        return -1;
    }

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) {
        SerialWrite("[VFS] Path strip failed\n");
        return -1;
    }

    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) local_path = "/";
            FsNode* node = FsFind(local_path);
            if (!node || node->type != FS_FILE) return -1;
            if (node->size == 0) return 0;
            if (node->data == NULL) return 0;
            uint32_t copy_size = (node->size < max_size) ? node->size : max_size;
            FastMemcpy(buffer, node->data, copy_size);
            return copy_size;
        }
        case VFS_FAT12: case VFS_FAT16: {
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            // Use new path-aware function
            return Fat1xReadFile(local_path, buffer, max_size);
        }
        case VFS_EXT2: {
            return Ext2ReadFile(local_path, buffer, max_size);
        }
    }

    return -1;
}

int VfsListDir(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) local_path = "/"; // normalize empty to root
            int result = FsListDir(local_path);

            // If listing root, also show mount points
            if (FastStrCmp(path, "/") == 0) {
                for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
                    if (mounts[i].active && FastStrCmp(mounts[i].mount_point, "/") != 0) {
                        PrintKernel("[MOUNT] ");
                        PrintKernel(mounts[i].mount_point + 1); // Skip leading /
                        PrintKernel(" (type=");
                        PrintKernelInt(mounts[i].type);
                        PrintKernel(")\n");
                    }
                }
            }
            return result;
        }
        case VFS_FAT12: case VFS_FAT16: {
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat1xListDirectory(local_path);
        }
        case VFS_EXT2: {
            return Ext2ListDir(local_path);
        }
    }

    return -1;
}

int VfsCreateFile(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) return -1;
            int fd = FsOpen(local_path, FS_WRITE);
            if (fd < 0) return -1;
            FsClose(fd);
            return 0;
        }
        case VFS_FAT12: case VFS_FAT16:
            if (FastStrlen(local_path, 2) == 0) return -1;
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat1xCreateFile(local_path);
        case VFS_EXT2: {
            return Ext2CreateFile(local_path);
        }
    }

    return -1;
}

int VfsCreateDir(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS:
            if (FastStrlen(local_path, 2) == 0) return -1;
            return FsMkdir(local_path);
        case VFS_FAT12: case VFS_FAT16:
            if (FastStrlen(local_path, 2) == 0) return -1;
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat1xCreateDir(local_path);
        case VFS_EXT2: {
            return Ext2CreateDir(local_path);
        }
    }

    return -1;
}

int VfsDelete(const char* path, bool Recursive) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS:
            if (FastStrlen(local_path, 2) == 0) return -1;
            if (Recursive) return FsDeleteRecursive(local_path);
            return FsDelete(local_path);
        case VFS_FAT12: case VFS_FAT16:
            if (FastStrlen(local_path, 2) == 0) return -1;
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            if (Recursive) return Fat1xDeleteRecursive(local_path);
            return Fat1xDeleteFile(local_path);
        case VFS_EXT2: {
            return Ext2Delete(local_path);
        }
    }

    return -1;
}

uint64_t VfsGetFileSize(const char* path) {
    if (!path) {
        SerialWrite("[VFS] VfsGetFileSize: NULL path\n");
        return 0;
    }

    if (FastStrlen(path, VFS_MAX_PATH_LEN) == 0) {
        SerialWrite("[VFS] VfsGetFileSize: Empty path\n");
        return 0;
    }

    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) {
        SerialWrite("[VFS] VfsGetFileSize: No mount found for path\n");
        return 0;
    }

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) {
        SerialWrite("[VFS] VfsGetFileSize: Path strip failed\n");
        return 0;
    }

    switch (mount->type) {
        case VFS_RAMFS: {
            // Normalize empty path to root
            if (FastStrlen(local_path, 2) == 0) local_path = "/";

            FsNode* node = FsFind(local_path);
            if (!node) {
                SerialWrite("[VFS] VfsGetFileSize: File not found in RAMFS\n");
                return 0;
            }

            if (node->type != FS_FILE) {
                SerialWrite("[VFS] VfsGetFileSize: Path is not a file in RAMFS\n");
                return 0;
            }

            SerialWrite("[VFS] VfsGetFileSize: RAMFS file found, size: ");
            SerialWriteDec((uint32_t)node->size);
            SerialWrite("\n");
            return node->size;
        }

        case VFS_FAT12: case VFS_FAT16: {
            extern int fat12_initialized;
            if (!fat12_initialized) return 0;
            return  Fat1xGetFileSize(local_path);

        }
        case VFS_EXT2: {
            return Ext2GetFileSize(local_path);
        }

        default:
            SerialWrite("[VFS] VfsGetFileSize: Unknown filesystem type\n");
            return 0;
    }
}

int VfsIsFile(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return 0;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return 0;

    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) local_path = "/";
            FsNode* node = FsFind(local_path);
            return node && node->type == FS_FILE;
        }
        case VFS_FAT12: case VFS_FAT16: {
            extern int fat12_initialized;
            if (!fat12_initialized) return 0;
            if (Fat1xIsDirectory(local_path)) return 0;
            uint64_t size = Fat1xGetFileSize(local_path);
            if (size > 0) return 1;
            char test_buffer[1];
            int read_result = Fat1xReadFile(local_path, test_buffer, 1);
            return read_result >= 0;
        }
        case VFS_EXT2: {
            return Ext2IsFile(local_path);
        }
    }

    return 0;
}

int VfsIsDir(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return 0;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS: {
            FsNode* node = FsFind(local_path);
            return node && node->type == FS_DIRECTORY;
        }
        case VFS_FAT12: case VFS_FAT16: {
            extern int fat12_initialized;
            if (!fat12_initialized) return 0;
            // Use new directory detection function
            return Fat1xIsDirectory(local_path);
        }
        case VFS_EXT2: {
            return Ext2IsDir(local_path);
        }
    }
    return 0;
}

int VfsWriteFile(const char* path, const void* buffer, uint32_t size) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) return -1;
            int fd = FsOpen(local_path, FS_WRITE);
            if (fd < 0) return -1;
            int result = FsWrite(fd, buffer, size);
            FsClose(fd);
            return result;
        }
        case VFS_FAT12: case VFS_FAT16:
            // Use enhanced path-aware file writing
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat1xWriteFile(local_path, buffer, size);
        case VFS_EXT2: {
            return Ext2WriteFile(local_path, buffer, size);
        }
    }

    return -1;
}

int VfsCopyFile(const char* src_path, const char* dest_path) {
    if (!src_path || !dest_path) {
        return -1;
    }
    
    WriteLock(&vfs_lock, MLFQGetCurrentProcess()->pid);
    
    // Ensure source exists and is a regular file
    if (!VfsIsFile(src_path)) {
        WriteUnlock(&vfs_lock);
        return -1;
    }
    uint64_t file_size = VfsGetFileSize(src_path);
    // Handle empty file explicitly
    if (file_size == 0) {
        int result = VfsCreateFile(dest_path);
        WriteUnlock(&vfs_lock);
        return result;
    }
    // Prevent size_t/u32 truncation
    size_t buf_size = (size_t)file_size;
    if ((uint64_t)buf_size != file_size || buf_size == 0) {
        WriteUnlock(&vfs_lock);
        return -1;
    }
    void* buffer = KernelMemoryAlloc(buf_size);
    if (!buffer) {
        PrintKernelError("Failed to allocate memory\n");
        WriteUnlock(&vfs_lock);
        return -1; // Failed to allocate memory
    }
    int bytes_read = VfsReadFile(src_path, buffer, (uint32_t)buf_size);
    if (bytes_read <= 0) {
        KernelFree(buffer);
        PrintKernelError("Failed to read sources\n");
        WriteUnlock(&vfs_lock);
        return -1; // Failed to read source file
    }
    int bytes_written = VfsWriteFile(dest_path, buffer, (uint32_t)bytes_read);
    KernelFree(buffer);
    if (bytes_written <= 0) {
        PrintKernelError("Failed to write destination\n");
        WriteUnlock(&vfs_lock);
        return -1; // Failed to write destination file
    }
    WriteUnlock(&vfs_lock);
    return 0; // Success
}

int VfsMoveFile(const char* src_path, const char* dest_path) {
    if (!src_path || !dest_path) {
        return -1;
    }

    VfsMountStruct* src_mount = VfsFindMount(src_path);
    VfsMountStruct* dest_mount = VfsFindMount(dest_path);

    if (src_mount == dest_mount) {
        // If on the same filesystem, we could implement a rename function
        // For now, we will copy and delete
    }

    if (VfsCopyFile(src_path, dest_path) == 0) {
        if (VfsDelete(src_path, false) == 0) {
            return 0; // Success
        } else {
            // Cleanup the copied file if delete fails
            VfsDelete(dest_path, false);
            return -1; // Failed to delete source file
        }
    }

    return -1; // Failed to copy file
}

