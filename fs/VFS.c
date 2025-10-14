#include "VFS.h"
#include "../mm/MemOps.h"
#include "BlockDevice.h"
#include "Console.h"
#include "EXT/Ext2.h"
#include "FAT/FAT1x.h"
#include "FileSystem.h"
#include "KernelHeap.h"
#include "NTFS.h"
#include "Serial.h"
#include "StringOps.h"
#include "VFRFS.h"
#include "stdbool.h"

#define VFS_MAX_PATH_LEN 256

static VfsMountStruct mounts[VFS_MAX_MOUNTS];

void VfsListMount() {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active) {
            PrintKernel("Mount Point: ");
            PrintKernel(mounts[i].mount_point);
            PrintKernel(" | Device: ");
            if (mounts[i].device) {
                PrintKernel(mounts[i].device->name);
            } else {
                PrintKernel("None");
            }
            PrintKernel(" | FS Driver: ");
            if (mounts[i].fs_driver) {
                PrintKernel(mounts[i].fs_driver->name);
            } else {
                PrintKernel("VFRFS");
            }
            PrintKernel("\n");
        }
    }
}

int VfsMount(const char* path, BlockDevice* device, FileSystemDriver* fs_driver) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            FastStrCopy(mounts[i].mount_point, path, 64);
            mounts[i].device = device;
            mounts[i].fs_driver = fs_driver;
            mounts[i].active = 1;

            if (FastStrCmp(path, "/") != 0) {
                FsMkdir(path);
            }

            return 0;
        }
    }
    return -1;
}

int VfsInit(void) {
    PrintKernel("VFS: Initializing Virtual File System...\n");

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = 0;
    }
    PrintKernel( "VFS: Mount table cleared\n");

    // Register filesystems
    static FileSystemDriver ntfs_driver = {"NTFS", NtfsDetect, NtfsMount};
    FileSystemRegister(&ntfs_driver);
    PrintKernel("VFS: NTFS driver registered\n");
    static FileSystemDriver fat_driver = {"FAT1x", Fat1xDetect, Fat1xMount};
    FileSystemRegister(&fat_driver);
    PrintKernel("VFS: FAT1x driver registered\n");
    static FileSystemDriver ext2_driver = {"EXT2", Ext2Detect, Ext2Mount};
    FileSystemRegister(&ext2_driver);
    PrintKernel("VFS: EXT2 driver registered\n");

    int result = VfsMount("/", NULL, NULL); // RamFS doesn't need a device or driver
    if (result != 0) {
        SerialWrite("VFS: Failed to mount root\n");
    }

    PrintKernelSuccess("VFS: Virtual File System initialized\n");
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
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return -1;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xReadFile(local_path, buffer, max_size);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2ReadFile(local_path, buffer, max_size);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsReadFile(local_path, buffer, max_size);
        }
    } else {
        FsNode* node = FsFind(local_path);
        if (!node || node->type != FS_FILE) return -1;
        if (node->size == 0) return 0;
        if (node->data == NULL) return 0;
        uint32_t copy_size = (node->size < max_size) ? node->size : max_size;
        FastMemcpy(buffer, node->data, copy_size);
        return copy_size;
    }

    return -1;
}

int VfsWriteFile(const char* path, const void* buffer, uint32_t size) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return -1;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xWriteFile(local_path, buffer, size);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2WriteFile(local_path, buffer, size);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsWriteFile(local_path, buffer, size);
        }
    } else {
        int fd = FsOpen(local_path, FS_WRITE);
        if (fd < 0) return -1;
        int result = FsWrite(fd, buffer, size);
        FsClose(fd);
        return result;
    }

    return -1;
}

int VfsListDir(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return -1;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xListDirectory(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2ListDir(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsListDir(local_path);
        }
    } else {
        return FsListDir(local_path);
    }

    return -1;
}

int VfsCreateFile(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return -1;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xCreateFile(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2CreateFile(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsCreateFile(local_path);
        }
    } else {
        int fd = FsOpen(local_path, FS_WRITE);
        if (fd < 0) return -1;
        FsClose(fd);
        return 0;
    }

    return -1;
}

int VfsCreateDir(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return -1;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xCreateDir(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2CreateDir(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsCreateDir(local_path);
        }
    } else {
        return FsMkdir(local_path);
    }

    return -1;
}

int VfsDelete(const char* path, bool Recursive) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return -1;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return -1;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            if (Recursive) return Fat1xDeleteRecursive(local_path);
            return Fat1xDeleteFile(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2Delete(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsDelete(local_path);
        }
    } else {
        if (Recursive) return FsDeleteRecursive(local_path);
        return FsDelete(local_path);
    }

    return -1;
}

int VfsIsDir(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return 0;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return 0;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xIsDirectory(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2IsDir(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsIsDir(local_path);
        }
    } else {
        FsNode* node = FsFind(local_path);
        return node && node->type == FS_DIRECTORY;
    }

    return 0;
}

int VfsIsFile(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return 0;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return 0;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            return Fat1xGetFileSize(local_path) > 0;
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2IsFile(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsIsFile(local_path);
        }
    } else {
        FsNode* node = FsFind(local_path);
        return node && node->type == FS_FILE;
    }

    return 0;
}

uint64_t VfsGetFileSize(const char* path) {
    VfsMountStruct* mount = VfsFindMount(path);
    if (!mount) return 0;

    const char* local_path = VfsStripMount(path, mount);
    if (!local_path) return 0;

    if (mount->fs_driver) {
        if (FastStrCmp(mount->fs_driver->name, "FAT1x") == 0) {
            Fat1xSetActive(mount->device);
            return Fat1xGetFileSize(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "EXT2") == 0) {
            Ext2SetActive(mount->device);
            return Ext2GetFileSize(local_path);
        } else if (FastStrCmp(mount->fs_driver->name, "NTFS") == 0) {
            NtfsSetActive(mount->device);
            return NtfsGetFileSize(local_path);
        }
    } else {
        FsNode* node = FsFind(local_path);
        if (!node || node->type != FS_FILE) return 0;
        return node->size;
    }

    return 0;
}

int VfsAppendFile(const char* path, const void* buffer, uint32_t size) {
    // This is a naive implementation. A proper implementation would need
    // to read the file, append the data in memory, and then write it back.
    // This is not efficient and not recommended for large files.
    uint64_t current_size = VfsGetFileSize(path);
    void* current_content = KernelMemoryAlloc(current_size);
    if (!current_content) {
        return -1;
    }

    int bytes_read = VfsReadFile(path, current_content, current_size);
    if (bytes_read < 0) {
        KernelFree(current_content);
        return -1;
    }

    void* new_content = KernelMemoryAlloc(current_size + size);
    if (!new_content) {
        KernelFree(current_content);
        return -1;
    }

    FastMemcpy(new_content, current_content, current_size);
    FastMemcpy(new_content + current_size, buffer, size);

    int bytes_written = VfsWriteFile(path, new_content, current_size + size);

    KernelFree(current_content);
    KernelFree(new_content);

    return bytes_written;
}

int VfsCopyFile(const char* src_path, const char* dest_path) {
    if (!src_path || !dest_path) {
        return -1;
    }

    if (!VfsIsFile(src_path)) {
        return -1;
    }

    uint64_t file_size = VfsGetFileSize(src_path);
    if (file_size == 0) {
        int result = VfsCreateFile(dest_path);
        return result;
    }

    if (VfsCreateFile(dest_path) != 0) {
        PrintKernelError("Failed to create destination file\n");
        return -1;
    }

    size_t buf_size = (size_t)file_size;
    if ((uint64_t)buf_size != file_size || buf_size == 0) {
        return -1;
    }

    void* buffer = KernelMemoryAlloc(buf_size);
    if (!buffer) {
        PrintKernelError("Failed to allocate memory\n");
        return -1;
    }

    int bytes_read = VfsReadFile(src_path, buffer, (uint32_t)buf_size);
    if (bytes_read <= 0) {
        KernelFree(buffer);
        PrintKernelError("Failed to read source\n");
        return -1;
    }

    int bytes_written = VfsWriteFile(dest_path, buffer, (uint32_t)bytes_read);
    KernelFree(buffer);

    if (bytes_written <= 0) {
        PrintKernelError("Failed to write destination\n");
        return -1;
    }

    return 0;
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