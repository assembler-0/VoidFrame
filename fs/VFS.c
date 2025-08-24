#include "VFS.h"
#include "Console.h"
#include "FAT12.h"
#include "Fs.h"
#include "MemOps.h"
#include "StringOps.h"
#include "Serial.h"

#define VFS_MAX_PATH_LEN 256

static VfsMountStruct mounts[VFS_MAX_MOUNTS];

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

    // Only mount FAT12 if it was successfully initialized
    extern int fat12_initialized;
    if (fat12_initialized) {
        PrintKernel("[VFS] Attempting FAT12 mount...\n");
        int disk_result = VfsMount("/Persistent", VFS_FAT12, 0);

        if (disk_result == 0) {
            SerialWrite("[VFS] FAT12 mounted at /Persistent\n");
        } else {
            SerialWrite("[VFS] FAT12 mount failed\n");
        }
    } else {
        PrintKernel("[VFS] Skipping FAT12 mount - Not initialized\n");
    }

    PrintKernelSuccess("[VFS] Virtual File System initialized\n");
    return 0;
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
    SerialWrite("[VFS] VfsReadFile called\n");
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
        case VFS_FAT12: {
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            // Use new path-aware function
            return Fat12ReadFile(local_path, buffer, max_size);
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
        case VFS_FAT12: {
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat12ListDirectory(local_path);
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
        case VFS_FAT12:
            if (FastStrlen(local_path, 2) == 0) return -1;
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat12CreateFile(local_path);
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
        case VFS_FAT12:
            if (FastStrlen(local_path, 2) == 0) return -1;
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat12CreateDir(local_path);
    }

    return -1;
}

int VfsDelete(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);

    switch (mount->type) {
        case VFS_RAMFS:
            if (FastStrlen(local_path, 2) == 0) return -1;
            return FsDelete(local_path);
        case VFS_FAT12:
            if (FastStrlen(local_path, 2) == 0) return -1;
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat12DeleteFile(local_path);
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

        case VFS_FAT12: {
            extern int fat12_initialized;
            if (!fat12_initialized) return 0;
            // assume
            char test_buffer[1];
            int result = Fat12ReadFile(local_path, test_buffer, 1);
            if (result < 0) return 0; // File doesn't exist

            return 1024 * 1024;

        }

        default:
            SerialWrite("[VFS] VfsGetFileSize: Unknown filesystem type\n");
            return 0;
    }
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
        case VFS_FAT12: {
            extern int fat12_initialized;
            if (!fat12_initialized) return 0;
            // Use new directory detection function
            return Fat12IsDirectory(local_path);
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
        case VFS_FAT12:
            // Use enhanced path-aware file writing
            extern int fat12_initialized;
            if (!fat12_initialized) return -1;
            return Fat12WriteFile(local_path, buffer, size);
    }
    
    return -1;
}

