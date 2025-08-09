#include "VFS.h"
#include "Console.h"
#include "FAT12.h"
#include "Fs.h"
#include "FsUtils.h"
#include "MemOps.h"
#include "StringOps.h"

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
    PrintKernel("[VFS] Mount table cleared\n");

    // Mount RamFS as root
    PrintKernel("[VFS] Mounting RamFS as root...\n");
    int result = VfsMount("/", VFS_RAMFS, 0);
    PrintKernel("[VFS] Root mount result: ");
    PrintKernelInt(result);
    PrintKernel("\n");

    // Try to mount FAT12 on /disk
    PrintKernel("[VFS] Attempting FAT12 mount...\n");
    int disk_result = VfsMount("/disk", VFS_FAT12, 0);
    PrintKernel("[VFS] Disk mount result: ");
    PrintKernelInt(disk_result);
    PrintKernel("\n");
    PrintKernelSuccess("[VFS] FAT12 mounted at /disk\n");

    // Test mount table integrity
    PrintKernel("[VFS] Testing mount table integrity:\n");
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active) {
            PrintKernel("[VFS] Mount ");
            PrintKernelInt(i);
            PrintKernel(": '");
            PrintKernel(mounts[i].mount_point);
            PrintKernel("' type=");
            PrintKernelInt(mounts[i].type);
            PrintKernel("\n");
        }
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
    int mount_len = FastStrlen(mount->mount_point, 64);
    if (mount_len == 1 && mount->mount_point[0] == '/') {
        return path; // Root mount
    }
    const char* local = path + mount_len;
    if (*local == '/') {
        local++;
    }
    return local;
}

int VfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;
    
    const char* local_path = VfsStripMount(path, mount);
    
    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) local_path = "/"; // normalize empty to root
            FsNode* node = FsFind(local_path);
            if (!node || node->type != FS_FILE) return -1;
            
            uint32_t copy_size = (node->size < max_size) ? node->size : max_size;
            FastMemcpy(buffer, node->data, copy_size);
            return copy_size;
        }
        case VFS_FAT12:
            return Fat12ReadFile(local_path, buffer, max_size);
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
        case VFS_FAT12:
            if (FastStrCmp(local_path, "/") == 0 || FastStrlen(local_path, 256) == 0) {
                return Fat12ListRoot();
            }
            return -1;
    }
    
    return -1;
}

int VfsCreateFile(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;
    
    const char* local_path = VfsStripMount(path, mount);
    
    switch (mount->type) {
        case VFS_RAMFS: {
            if (FastStrlen(local_path, 2) == 0) return -1; // don't create at root with empty name
            int fd = FsOpen(local_path, FS_WRITE);
            if (fd < 0) return -1;
            FsClose(fd);
            return 0;
        }
        case VFS_FAT12:
            return -1; // FAT12 write not implemented yet
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
            return -1;
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
            return -1;
    }
    
    return -1;
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
            // Only root listing supported for FAT12 currently; treat "/disk" as a directory
            return FastStrlen(local_path, 256) == 0;
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
            return -1;
    }
    
    return -1;
}