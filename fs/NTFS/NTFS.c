#include "NTFS.h"
#include "BlockDevice.h"
#include "Console.h"
#include "FileSystem.h"
#include "Io.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "Panic.h"
#include "VFS.h"

static struct BlockDevice* ntfs_device = NULL;
static NtfsBootSector boot_sector;
static uint32_t bytes_per_sector;
static uint32_t sectors_per_cluster;
static uint32_t bytes_per_cluster;
static uint64_t mft_cluster;

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
    
    if (device->read_blocks(device, 0, 1, &boot_sector) != 0) return -1;
    
    ntfs_device = device;
    bytes_per_sector = boot_sector.bytes_per_sector;
    sectors_per_cluster = boot_sector.sectors_per_cluster;
    bytes_per_cluster = bytes_per_sector * sectors_per_cluster;
    mft_cluster = boot_sector.mft_cluster;

    VfsCreateDir(mount_point);
    if (VfsMount(mount_point, device, &ntfs_driver) != 0) {
        PrintKernelF("NTFS: Failed to register mount point %s\n", mount_point);
        return -1;
    }

    PrintKernel("NTFS: Mounted at ");
    PrintKernel(mount_point);
    PrintKernel("\n");

    return 0;
}

int NtfsReadMftRecord(uint64_t record_num, NtfsMftRecord* record) {
    if (!ntfs_device || !record) return -1;
    
    uint64_t mft_offset = mft_cluster * sectors_per_cluster;
    uint64_t record_offset = mft_offset + (record_num * 1024 / bytes_per_sector);
    
    return ntfs_device->read_blocks(ntfs_device, record_offset, 2, record);
}

uint64_t NtfsPathToMftRecord(const char* path) {
    // Simplified: return root directory for now
    if (!path || path[0] != '/') return 0;
    if (path[1] == '\0') return 5; // Root directory is MFT record 5
    
    // TODO: Implement directory traversal
    return 0;
}

int NtfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (!path || !buffer || !ntfs_device) return -1;
    
    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return -1;
    
    NtfsMftRecord* record = KernelMemoryAlloc(1024);
    if (!record) return -1;
    
    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
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
                return data_size;
            }
            // TODO: Handle non-resident data
        }
        
        attr_ptr += attr->length;
    }
    
    KernelFree(record);
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