#include "NTFS.h"
#include "BlockDevice.h"
#include "Console.h"
#include "FileSystem.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "VFS.h"
#include "SpinlockRust.h"
#include "Scheduler.h"

typedef struct {
    struct BlockDevice* device;
    NtfsBootSector boot_sector;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint64_t mft_cluster;
    RustRwLock* lock;
} NtfsVolume;

static NtfsVolume volume;

int NtfsDetect(struct BlockDevice* device) {
    if (!device || !device->read_blocks) return 0;
    
    NtfsBootSector boot;
    if (device->read_blocks(device, 0, 1, &boot) != 0) return 0;
    
    // Check NTFS signature
    if (boot.signature != 0xAA55) return 0;
    if (memcmp(boot.oem_id, "NTFS    ", 8) != 0) return 0;
    
    return 1;
}

static FileSystemDriver ntfs_driver = {"NTFS", NtfsDetect, NtfsMount};

int NtfsMount(struct BlockDevice* device, const char* mount_point) {
    if (!device || !device->read_blocks) return -1;
    
    if (!volume.lock) volume.lock = rust_rwlock_new();
    if (!volume.lock) {
        PrintKernel("NTFS: Failed to allocate lock\n");
        return -1;
    }
    
    rust_rwlock_write_lock(volume.lock, GetCurrentProcess()->pid);
    
    if (device->read_blocks(device, 0, 1, &volume.boot_sector) != 0) {
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    
    volume.device = device;
    volume.bytes_per_sector = volume.boot_sector.bytes_per_sector;
    volume.sectors_per_cluster = volume.boot_sector.sectors_per_cluster;
    volume.bytes_per_cluster = volume.bytes_per_sector * volume.sectors_per_cluster;
    volume.mft_cluster = volume.boot_sector.mft_cluster;

    VfsCreateDir(mount_point);
    if (VfsMount(mount_point, device, &ntfs_driver) != 0) {
        PrintKernel("NTFS: Failed to register mount point ");
        PrintKernel(mount_point);
        PrintKernel("\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    PrintKernel("NTFS: Mounted at ");
    PrintKernel(mount_point);
    PrintKernel("\n");
    
    rust_rwlock_write_unlock(volume.lock);
    return 0;
}

int NtfsReadMftRecord(uint64_t record_num, NtfsMftRecord* record) {
    if (!volume.device || !record) return -1;
    
    uint64_t mft_offset = volume.mft_cluster * volume.sectors_per_cluster;
    uint64_t record_offset = mft_offset + (record_num * 1024 / volume.bytes_per_sector);
    
    return volume.device->read_blocks(volume.device, record_offset, 2, record);
}

uint64_t NtfsPathToMftRecord(const char* path) {
    // Simplified: return root directory for now
    if (!path || path[0] != '/') return 0;
    if (path[1] == '\0') return 5; // Root directory is MFT record 5
    
    // TODO: Implement directory traversal
    return 0;
}

int NtfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (!path || !buffer || !volume.device) return -1;
    
    rust_rwlock_read_lock(volume.lock, GetCurrentProcess()->pid);
    
    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) {
        rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
        return -1;
    }
    
    NtfsMftRecord* record = KernelMemoryAlloc(1024);
    if (!record) {
        rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
        return -1;
    }
    
    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
        return -1;
    }
    
    // Find DATA attribute
    uint8_t* attr_ptr = (uint8_t*)record + record->attrs_offset;
    while (attr_ptr < (uint8_t*)record + record->bytes_in_use) {
        NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;
        
        if (attr->type == NTFS_ATTR_DATA) {
            if (!attr->non_resident) {
                // Resident data
                uint32_t data_size = attr->resident.value_length;
                if (data_size > max_size) data_size = max_size;
                
                memcpy(buffer, attr_ptr + attr->resident.value_offset, data_size);
                KernelFree(record);
                rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
                return data_size;
            }
            // TODO: Handle non-resident data
        }
        
        attr_ptr += attr->length;
    }
    
    KernelFree(record);
    rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
    return -1;
}

int NtfsListDir(const char* path) {
    PrintKernel("NTFS: Directory listing not implemented\n");
    return -1;
}

int NtfsIsFile(const char* path) {
    return 0; // TODO: Implement
}

int NtfsIsDir(const char* path) {
    if (!path) return 0;
    if (path[0] == '/' && path[1] == '\0') return 1; // Root is directory
    return 0; // TODO: Implement
}

uint64_t NtfsGetFileSize(const char* path) {
    return 0; // TODO: Implement
}
int NtfsWriteFile(const char* path, const void* buffer, uint32_t size) {
    PrintKernel("NTFS: Write operations not supported (read-only filesystem)\n");
    return -1;
}

int NtfsCreateFile(const char* path) {
    PrintKernel("NTFS: File creation not supported (read-only filesystem)\n");
    return -1;
}

int NtfsCreateDir(const char* path) {
    PrintKernel("NTFS: Directory creation not supported (read-only filesystem)\n");
    return -1;
}

int NtfsDelete(const char* path) {
    PrintKernel("NTFS: Delete operations not supported (read-only filesystem)\n");
    return -1;
}

void NtfsInit(void) {
    FileSystemRegister(&ntfs_driver);
}