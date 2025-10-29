#include "NTFS.h"
#include "BlockDevice.h"
#include "Console.h"
#include "FileSystem.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "VFS.h"
#include "SpinlockRust.h"
#include "Scheduler.h"
#include "Rtc.h"

typedef struct {
    struct BlockDevice* device;
    NtfsBootSector boot_sector;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint64_t mft_cluster;
    RustRwLock* lock;
} NtfsVolume;

static NtfsVolume* g_ntfs_by_dev[MAX_BLOCK_DEVICES] = {0};
static NtfsVolume* g_ntfs_active = NULL;
#define volume (*g_ntfs_active)

void NtfsSetActive(BlockDevice* device) {
    if (!device) { g_ntfs_active = NULL; return; }
    int id = device->id;
    if (id < 0 || id >= MAX_BLOCK_DEVICES) { g_ntfs_active = NULL; return; }
    g_ntfs_active = g_ntfs_by_dev[id];
}

int NtfsDetect(struct BlockDevice* device) {
    if (!device || !device->read_blocks) return 0;
    
    NtfsBootSector boot;
    if (device->read_blocks(device, 0, 1, &boot) != 0) return 0;
    
    // Check NTFS signature
    if (boot.signature != 0xAA55) return 0;
    if (memcmp(boot.oem_id, "NTFS    ", 8) != 0) return 0;
    
    return 1;
}

static FileSystemDriver ntfs_driver = {"NTFS", NtfsDetect, NtfsMount, NtfsUnmount};

int NtfsMount(struct BlockDevice* device, const char* mount_point) {
    if (!device || !device->read_blocks) return -1;
    if (device->id < 0 || device->id >= MAX_BLOCK_DEVICES) return -1;
    NtfsVolume* vol = g_ntfs_by_dev[device->id];
    if (!vol) {
        vol = (NtfsVolume*)KernelMemoryAlloc(sizeof(NtfsVolume));
        if (!vol) return -1;
        FastMemset(vol, 0, sizeof(NtfsVolume));
        g_ntfs_by_dev[device->id] = vol;
    }
    g_ntfs_active = vol;

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

    if (volume.boot_sector.bytes_per_sector == 0 || volume.boot_sector.bytes_per_sector > 4096) {
        PrintKernel("NTFS: Invalid bytes_per_sector\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    if (volume.boot_sector.sectors_per_cluster == 0) {
        PrintKernel("NTFS: Invalid sectors_per_cluster\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    volume.device = device;
    volume.bytes_per_sector = volume.boot_sector.bytes_per_sector;
    volume.sectors_per_cluster = volume.boot_sector.sectors_per_cluster;
    // Check for overflow in bytes_per_cluster calculation
    if (volume.bytes_per_sector > UINT32_MAX / volume.sectors_per_cluster) {
        PrintKernel("NTFS: bytes_per_cluster overflow\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    volume.bytes_per_cluster = volume.bytes_per_sector * volume.sectors_per_cluster;
    // Validate bytes_per_cluster is reasonable
    if (volume.bytes_per_cluster == 0 || volume.bytes_per_cluster > 1048576) {
        PrintKernel("NTFS: Invalid bytes_per_cluster\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    volume.mft_cluster = volume.boot_sector.mft_cluster;
    
    // Debug and validate MFT cluster
    PrintKernel("NTFS: MFT cluster: ");
    PrintKernelHex(volume.mft_cluster);
    PrintKernel("\n");
    
    if (volume.mft_cluster == 0) {
        PrintKernel("NTFS: Invalid MFT cluster location (0)\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    
    // Validate MFT cluster is within volume bounds
    if (volume.mft_cluster >= volume.boot_sector.total_sectors / volume.sectors_per_cluster) {
        PrintKernel("NTFS: MFT cluster beyond volume bounds\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    VfsCreateDir(mount_point);
    if (VfsMount(mount_point, device, &ntfs_driver) != 0) {
        PrintKernel("NTFS: Failed to register mount point ");
        PrintKernel(mount_point);
        PrintKernel("\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    // Debug boot sector fields
    PrintKernel("NTFS: bytes_per_sector: ");
    PrintKernelHex(volume.bytes_per_sector);
    PrintKernel("\n");
    PrintKernel("NTFS: sectors_per_cluster: ");
    PrintKernelHex(volume.sectors_per_cluster);
    PrintKernel("\n");
    PrintKernel("NTFS: total_sectors: ");
    PrintKernelHex(volume.boot_sector.total_sectors);
    PrintKernel("\n");
    PrintKernel("NTFS: clusters_per_file_record: ");
    PrintKernelHex((uint32_t)volume.boot_sector.clusters_per_file_record);
    PrintKernel("\n");
    
    PrintKernel("NTFS: Mounted at ");
    PrintKernel(mount_point);
    PrintKernel("\n");
    
    rust_rwlock_write_unlock(volume.lock);
    return 0;
}

int NtfsUnmount(BlockDevice* device) {
    if (!device) return -1;
    int id = device->id;
    if (id < 0 || id >= MAX_BLOCK_DEVICES) return -1;

    NtfsVolume* vol = g_ntfs_by_dev[id];
    if (!vol) return -1; // Not mounted

    if (g_ntfs_active == vol) {
        g_ntfs_active = NULL;
    }

    if (vol->lock) {
        rust_rwlock_free(vol->lock);
    }

    KernelFree(vol);
    g_ntfs_by_dev[id] = NULL;

    return 0;
}

int NtfsReadMftRecord(uint64_t record_num, NtfsMftRecord* record) {
    if (!volume.device || !record) return -1;
    if (!volume.lock) return -1;
    rust_rwlock_read_lock(volume.lock, GetCurrentProcess()->pid);

    // Calculate MFT record size from boot sector
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        // Negative value means 2^(-clusters_per_file_record) bytes
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    // Calculate byte offset of the MFT record
    uint64_t mft_start_sector = volume.mft_cluster * volume.sectors_per_cluster;
    uint64_t record_byte_offset = record_num * mft_record_size;
    uint64_t record_sector = mft_start_sector + (record_byte_offset / volume.bytes_per_sector);
    
    // Calculate how many sectors we need to read
    uint32_t sectors_needed = (mft_record_size + volume.bytes_per_sector - 1) / volume.bytes_per_sector;
    
    int result = volume.device->read_blocks(volume.device, record_sector, sectors_needed, record);
    rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
    return result;
}

// Minimal parser: extract only the first run from a non-resident attribute's mapping pairs
// Supports a single contiguous run and returns its absolute LCN and length in clusters.
static int NtfsParseFirstRun(const NtfsAttrHeader* attr, int64_t* out_lcn, uint64_t* out_clusters) {
    if (!attr || !out_lcn || !out_clusters) return -1;
    uint16_t mpoff = attr->nonresident.mapping_pairs_offset;
    if (mpoff == 0) return -1;
    const uint8_t* mp = (const uint8_t*)((const uint8_t*)attr + mpoff);
    uint8_t hdr = *mp++;
    if (hdr == 0) return -1;
    uint8_t len_len = hdr & 0x0F;
    uint8_t off_len = (hdr >> 4) & 0x0F;
    if (len_len == 0 || len_len > 8 || off_len > 8) return -1;

    uint64_t clusters = 0;
    for (uint8_t i = 0; i < len_len; i++) {
        clusters |= ((uint64_t)mp[i]) << (8 * i);
    }
    mp += len_len;

    int64_t lcn_delta = 0;
    if (off_len > 0) {
        int64_t tmp = 0;
        for (uint8_t i = 0; i < off_len; i++) {
            tmp |= ((int64_t)mp[i]) << (8 * i);
        }
        // Sign-extend if highest bit of the most significant byte is set
        if (mp[off_len - 1] & 0x80) {
            tmp |= -((int64_t)1 << (off_len * 8));
        }
        lcn_delta = tmp; // First run delta from 0
    }

    if (clusters == 0 || lcn_delta < 0) return -1; // Don't support sparse or negative LCN in this minimal parser

    *out_clusters = clusters;
    *out_lcn = lcn_delta; // absolute LCN for the first run
    return 0;
}

// Return MFT record number for a path in root (single component). 0 on failure.
static const char* NtfsBaseName(const char* path) {
    if (!path) return NULL;
    const char* s = path;
    const char* last = path;
    while (*s) {
        if (*s == '/') last = s + 1;
        s++;
    }
    if (*last == '\0') return NULL;
    return last;
}

static int NtfsUtf16EqualsAscii(const uint16_t* u16, uint8_t len16, const char* ascii) {
    if (!u16 || !ascii) return 0;
    size_t n = 0;
    while (ascii[n] != '\0') n++;
    if ((size_t)len16 != n) return 0;
    for (size_t i = 0; i < n; i++) {
        if ((uint8_t)u16[i] != (uint8_t)ascii[i] || (u16[i] >> 8) != 0) return 0;
    }
    return 1;
}

// Helper: find child record by scanning MFT for a FILE_NAME with given parent and name (ASCII)
static uint64_t NtfsFindChild(uint64_t parent_mft, const char* name, uint32_t mft_record_size, NtfsMftRecord* rec_buf) {
    const uint32_t max_scan = 4096;
    for (uint32_t i = 0; i < max_scan; i++) {
        if (NtfsReadMftRecord(i, rec_buf) != 0) continue;
        if (rec_buf->flags == 0) continue;
        uint8_t* attr_ptr = (uint8_t*)rec_buf + rec_buf->attrs_offset;
        uint8_t* rec_end = (uint8_t*)rec_buf + mft_record_size;
        while (attr_ptr + sizeof(NtfsAttrHeader) <= rec_end &&
               attr_ptr < (uint8_t*)rec_buf + rec_buf->bytes_in_use) {
            NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;
            if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader)) break;
            if (attr_ptr + attr->length > rec_end) break;
            if (attr->type == NTFS_ATTR_FILENAME && !attr->non_resident) {
                NtfsFilename* fn = (NtfsFilename*)(attr_ptr + attr->resident.value_offset);
                if ((uint64_t)fn->parent_directory == parent_mft && fn->filename_length > 0) {
                    if (NtfsUtf16EqualsAscii(fn->filename, fn->filename_length, name)) {
                        return i;
                    }
                }
                break; // assume single FILE_NAME
            }
            attr_ptr += attr->length;
        }
    }
    return 0;
}

// Resolve parent directory record and extract basename into provided buffer.
static int NtfsResolveParentAndName(const char* path, uint64_t* out_parent, char* name_buf, size_t name_buf_len) {
    if (!path || path[0] != '/' || !out_parent || !name_buf || name_buf_len == 0) return -1;

    // Determine MFT record size
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1u << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    NtfsMftRecord* rec = KernelMemoryAlloc(mft_record_size);
    if (!rec) return -1;

    uint64_t current = 5; // start at root
    const char* p = path + 1;
    const char* last_name_start = NULL;

    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        const char* comp_start = p;
        while (*p && *p != '/') p++;
        size_t comp_len = (size_t)(p - comp_start);
        if (comp_len == 0) break;
        // Check if there are more components after this one
        const char* q = p;
        while (*q == '/') q++;
        int has_more = (*q != '\0');
        if (has_more) {
            // Resolve this component as existing directory/file
            char temp[256];
            if (comp_len >= sizeof(temp)) { KernelFree(rec); return -1; }
            for (size_t i = 0; i < comp_len; i++) temp[i] = comp_start[i];
            temp[comp_len] = '\0';
            uint64_t child = NtfsFindChild(current, temp, mft_record_size, rec);
            if (child == 0) { KernelFree(rec); return -1; }
            current = child;
        } else {
            // This is the final component: basename for creation
            if (comp_len >= name_buf_len) { KernelFree(rec); return -1; }
            for (size_t i = 0; i < comp_len; i++) name_buf[i] = comp_start[i];
            name_buf[comp_len] = '\0';
            last_name_start = comp_start;
            break;
        }
    }

    KernelFree(rec);
    if (!last_name_start) return -1;
    *out_parent = current;
    return 0;
}

uint64_t NtfsPathToMftRecord(const char* path) {
    if (!path || path[0] != '/') return 0;
    if (path[1] == '\0') return 5; // Root directory is MFT record 5

    // Determine MFT record size
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1u << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    NtfsMftRecord* rec = KernelMemoryAlloc(mft_record_size);
    if (!rec) return 0;

    uint64_t current = 5; // start at root
    const char* p = path + 1; // skip leading '/'
    while (*p) {
        // Skip consecutive slashes
        while (*p == '/') p++;
        if (*p == '\0') break;
        // Find end of this component
        const char* comp_start = p;
        while (*p && *p != '/') p++;
        size_t comp_len = (size_t)(p - comp_start);
        if (comp_len == 0) break;
        // Copy component to temporary stack buffer (limit reasonable length)
        char name[256];
        if (comp_len >= sizeof(name)) { KernelFree(rec); return 0; }
        for (size_t i = 0; i < comp_len; i++) name[i] = comp_start[i];
        name[comp_len] = '\0';
        // Look up this child under current
        uint64_t child = NtfsFindChild(current, name, mft_record_size, rec);
        if (child == 0) { KernelFree(rec); return 0; }
        current = child;
    }

    KernelFree(rec);
    return current;
}

int NtfsReadFile(const char* path, void* buffer, uint32_t max_size) {
    if (!path || !buffer) return -1;
    if (!volume.lock) return -1;

    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return -1;

    // Calculate MFT record size
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }
    
    NtfsMftRecord* record = KernelMemoryAlloc(mft_record_size);
    if (!record) return -1;

    rust_rwlock_read_lock(volume.lock, GetCurrentProcess()->pid);

    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
        return -1;
    }

    // Find DATA attribute
    uint8_t* attr_ptr   = (uint8_t*)record + record->attrs_offset;
    uint8_t* record_end = (uint8_t*)record + mft_record_size;

    // Validate bytes_in_use before parsing
    if (record->bytes_in_use > mft_record_size) {
        KernelFree(record);
        rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
        return -1;
    }

    while (attr_ptr + sizeof(NtfsAttrHeader) <= record_end &&
           attr_ptr < (uint8_t*)record + record->bytes_in_use) {
        NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;

        // Ensure length is sane
        if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader))
            break;
        if (attr_ptr + attr->length > record_end)
            break;

        if (attr->type == NTFS_ATTR_DATA && !attr->non_resident) {
            // Resident data
            uint32_t data_size = attr->resident.value_length;
            if (data_size > max_size) data_size = max_size;

            // Validate value offset and total size
            uint8_t* data_ptr = attr_ptr + attr->resident.value_offset;
            if (data_ptr + data_size > record_end) {
                KernelFree(record);
            rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
                return -1;
            }

            memcpy(buffer, data_ptr, data_size);
            KernelFree(record);
            rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
            return (int)data_size;
        }

        attr_ptr += attr->length;
    }

    KernelFree(record);
    rust_rwlock_read_unlock(volume.lock, GetCurrentProcess()->pid);
    return -1;
}

int NtfsListDir(const char* path) {
    if (!path || path[0] != '/') return -1;

    // Determine MFT record size
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1u << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    uint64_t dir_mft = (path[0] == '/' && path[1] == '\0') ? 5 : NtfsPathToMftRecord(path);
    if (dir_mft == 0) return -1;

    NtfsMftRecord* rec = KernelMemoryAlloc(mft_record_size);
    if (!rec) return -1;

    // Optionally verify that target is a directory
    if (NtfsReadMftRecord(dir_mft, rec) == 0) {
        if ((rec->flags & 0x2) == 0) {
            KernelFree(rec);
            return -1; // not a directory
        }
    }

    const uint32_t max_scan = 4096;
    for (uint32_t i = 0; i < max_scan; i++) {
        if (NtfsReadMftRecord(i, rec) != 0) continue;
        if (rec->flags == 0) continue;
        uint8_t* attr_ptr = (uint8_t*)rec + rec->attrs_offset;
        uint8_t* rec_end = (uint8_t*)rec + mft_record_size;
        while (attr_ptr + sizeof(NtfsAttrHeader) <= rec_end &&
               attr_ptr < (uint8_t*)rec + rec->bytes_in_use) {
            NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;
            if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader)) break;
            if (attr_ptr + attr->length > rec_end) break;
            if (attr->type == NTFS_ATTR_FILENAME && !attr->non_resident) {
                NtfsFilename* fn = (NtfsFilename*)(attr_ptr + attr->resident.value_offset);
                if ((uint64_t)fn->parent_directory == dir_mft && fn->filename_length > 0) {
                    // Print ASCII subset of UTF-16
                    for (uint8_t k = 0; k < fn->filename_length; k++) {
                        char ch = (char)(uint8_t)fn->filename[k];
                        if (ch == '\0') break;
                        PrintKernelChar(ch);
                    }
                    PrintKernel("\n");
                }
                break; // assume single FILE_NAME
            }
            attr_ptr += attr->length;
        }
    }
    KernelFree(rec);
    return 0;
}

int NtfsIsFile(const char* path) {
    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return 0;

    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }
    
    NtfsMftRecord* record = KernelMemoryAlloc(mft_record_size);
    if (!record) return 0;

    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        return 0;
    }

    int is_file = (record->flags & 0x2) == 0;
    KernelFree(record);
    return is_file;
}

int NtfsIsDir(const char* path) {
    if (!path) return 0;
    if (path[0] == '/' && path[1] == '\0') return 1; // Root is directory

    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return 0;

    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }
    
    NtfsMftRecord* record = KernelMemoryAlloc(mft_record_size);
    if (!record) return 0;

    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        return 0;
    }

    int is_dir = (record->flags & 0x2) != 0;
    KernelFree(record);
    return is_dir;
}

uint64_t NtfsGetFileSize(const char* path) {
    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return 0;

    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }
    
    NtfsMftRecord* record = KernelMemoryAlloc(mft_record_size);
    if (!record) return 0;

    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        return 0;
    }

    uint8_t* attr_ptr = (uint8_t*)record + record->attrs_offset;
    uint8_t* record_end = (uint8_t*)record + mft_record_size;

    while (attr_ptr + sizeof(NtfsAttrHeader) <= record_end &&
           attr_ptr < (uint8_t*)record + record->bytes_in_use) {
        NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;

        if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader)) break;
        if (attr_ptr + attr->length > record_end) break;

        if (attr->type == NTFS_ATTR_DATA) {
            if (attr->non_resident) {
                uint64_t size = attr->nonresident.data_size;
                KernelFree(record);
                return size;
            } else {
                uint64_t size = attr->resident.value_length;
                KernelFree(record);
                return size;
            }
        }

        attr_ptr += attr->length;
    }

    KernelFree(record);
    return 0;
}
int NtfsWriteFile(const char* path, const void* buffer, uint32_t size) {
    if (!path || !buffer) return -1;
    if (!volume.lock) return -1;

    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return -1;

    NtfsMftRecord* record = KernelMemoryAlloc(1024);
    if (!record) return -1;

    rust_rwlock_write_lock(volume.lock, GetCurrentProcess()->pid);

    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    uint8_t* attr_ptr   = (uint8_t*)record + record->attrs_offset;
    uint8_t* record_end = (uint8_t*)record + 1024;

    if (record->bytes_in_use > 1024) {
        KernelFree(record);
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    while (attr_ptr + sizeof(NtfsAttrHeader) <= record_end &&
           attr_ptr < (uint8_t*)record + record->bytes_in_use) {
        NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;

        if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader))
            break;
        if (attr_ptr + attr->length > record_end)
            break;

        if (attr->type == NTFS_ATTR_DATA && !attr->non_resident) {
            // Resident data
            uint32_t data_size = attr->resident.value_length;
            if (size > data_size) {
                // Not enough space in resident attribute
                KernelFree(record);
                rust_rwlock_write_unlock(volume.lock);
                return -1; 
            }

            uint8_t* data_ptr = attr_ptr + attr->resident.value_offset;
            if (data_ptr + size > record_end) {
                KernelFree(record);
                rust_rwlock_write_unlock(volume.lock);
                return -1;
            }

            memcpy(data_ptr, buffer, size);

            // Write back the MFT record
            uint64_t mft_offset = volume.mft_cluster * volume.sectors_per_cluster;
            uint64_t record_offset = mft_offset + (mft_record_num * 1024 / volume.bytes_per_sector);
            
            if (volume.device->write_blocks(volume.device, record_offset, 2, record) != 0) {
                KernelFree(record);
                rust_rwlock_write_unlock(volume.lock);
                return -1;
            }

            KernelFree(record);
            rust_rwlock_write_unlock(volume.lock);
            return (int)size;
        }

        attr_ptr += attr->length;
    }

    KernelFree(record);
    rust_rwlock_write_unlock(volume.lock);
    return -1;
}

static uint64_t NtfsAllocateMftRecord() {
    // Calculate MFT record size
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }
    
    // The MFT allocation bitmap is the $BITMAP (0xB0) attribute of $MFT (record 0)
    NtfsMftRecord* mft_record = KernelMemoryAlloc(mft_record_size);
    if (!mft_record) {
        PrintKernel("NTFS: Failed to allocate $MFT record buffer\n");
        return 0;
    }

    if (NtfsReadMftRecord(0, mft_record) != 0) {
        PrintKernel("NTFS: Failed to read $MFT (record 0)\n");
        KernelFree(mft_record);
        return 0;
    }

    // Find the $BITMAP attribute of the $MFT file
    uint8_t* attr_ptr = (uint8_t*)mft_record + mft_record->attrs_offset;
    uint8_t* record_end = (uint8_t*)mft_record + mft_record_size;

    while (attr_ptr + sizeof(NtfsAttrHeader) <= record_end &&
           attr_ptr < (uint8_t*)mft_record + mft_record->bytes_in_use) {
        NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;

        if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader)) break;
        if (attr_ptr + attr->length > record_end) break;

        if (attr->type == NTFS_ATTR_BITMAP) {
            if (!attr->non_resident) {
                uint8_t* bitmap_data = attr_ptr + attr->resident.value_offset;
                uint32_t bitmap_size = attr->resident.value_length;

                // Find the first free bit
                for (uint32_t i = 0; i < bitmap_size; i++) {
                    if (bitmap_data[i] != 0xFF) {
                        for (int j = 0; j < 8; j++) {
                            if (!(bitmap_data[i] & (1 << j))) {
                                bitmap_data[i] |= (1 << j); // Mark as used

                                // Write the modified $MFT record back (bitmap is resident here)
                                uint64_t mft_start_sector = volume.mft_cluster * volume.sectors_per_cluster;
                                uint64_t record_byte_offset = 0 * mft_record_size;
                                uint64_t record_sector = mft_start_sector + (record_byte_offset / volume.bytes_per_sector);
                                uint32_t sectors_needed = (mft_record_size + volume.bytes_per_sector - 1) / volume.bytes_per_sector;
                                volume.device->write_blocks(volume.device, record_sector, sectors_needed, mft_record);

                                KernelFree(mft_record);
                                return (uint64_t)i * 8 + j;
                            }
                        }
                    }
                }
            } else {
                // Minimal support for non-resident $MFT::$BITMAP with a single contiguous run
                int64_t lcn = -1;
                uint64_t run_clusters = 0;
                if (NtfsParseFirstRun(attr, &lcn, &run_clusters) != 0 || lcn < 0 || run_clusters == 0) {
                    PrintKernel("NTFS: $MFT::$BITMAP non-resident with unsupported mapping pairs\n");
                    KernelFree(mft_record);
                    return 0;
                }
                uint64_t total_bytes = run_clusters * (uint64_t)volume.bytes_per_cluster;
                if (attr->nonresident.data_size < total_bytes) total_bytes = attr->nonresident.data_size;
                if (total_bytes == 0) {
                    PrintKernel("NTFS: $MFT::$BITMAP non-resident but empty\n");
                    KernelFree(mft_record);
                    return 0;
                }
                uint32_t sectors = (uint32_t)((total_bytes + volume.bytes_per_sector - 1) / volume.bytes_per_sector);
                uint8_t* bitmap_buf = KernelMemoryAlloc((size_t)sectors * volume.bytes_per_sector);
                if (!bitmap_buf) {
                    PrintKernel("NTFS: Failed to alloc bitmap buffer\n");
                    KernelFree(mft_record);
                    return 0;
                }
                uint64_t start_sector = (uint64_t)lcn * volume.sectors_per_cluster;
                if (volume.device->read_blocks(volume.device, start_sector, sectors, bitmap_buf) != 0) {
                    KernelFree(bitmap_buf);
                    KernelFree(mft_record);
                    PrintKernel("NTFS: Failed to read non-resident $MFT::$BITMAP\n");
                    return 0;
                }
                // Scan for a free bit within this first run portion
                uint64_t bytes_scanned = (uint64_t)sectors * volume.bytes_per_sector;
                for (uint64_t i = 0; i < bytes_scanned; i++) {
                    if (bitmap_buf[i] != 0xFF) {
                        for (int j = 0; j < 8; j++) {
                            if (!(bitmap_buf[i] & (1u << j))) {
                                bitmap_buf[i] |= (1u << j);
                                // Write back
                                if (volume.device->write_blocks(volume.device, start_sector, sectors, bitmap_buf) != 0) {
                                    PrintKernel("NTFS: Failed to write non-resident $MFT::$BITMAP\n");
                                    KernelFree(bitmap_buf);
                                    KernelFree(mft_record);
                                    return 0;
                                }
                                uint64_t rec = (uint64_t)i * 8 + (uint64_t)j;
                                KernelFree(bitmap_buf);
                                KernelFree(mft_record);
                                return rec;
                            }
                        }
                    }
                }
                KernelFree(bitmap_buf);
                KernelFree(mft_record);
                return 0;
            }
        }
        attr_ptr += attr->length;
    }

    KernelFree(mft_record);
    return 0; // No free records found
}

int NtfsCreateFile(const char* path) {
    if (!path || !volume.lock) return -1;

    uint64_t parent_mft = 5;
    char name[256];
    if (NtfsResolveParentAndName(path, &parent_mft, name, sizeof(name)) != 0) return -1;

    rust_rwlock_write_lock(volume.lock, GetCurrentProcess()->pid);

    uint64_t mft_record_num = NtfsAllocateMftRecord();
    if (mft_record_num == 0) {
        PrintKernel("NTFS: No free MFT records\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1; // No free MFT records
    }

    // Determine MFT record size
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1u << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    NtfsMftRecord* record = KernelMemoryAlloc(mft_record_size);
    if (!record) {
        PrintKernel("NTFS: Failed to allocate memory for MFT record\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    memset(record, 0, mft_record_size);

    // Initialize MFT record header
    record->signature = 'ELIF';
    record->flags = 1; // In-use (file)
    record->attrs_offset = sizeof(NtfsMftRecord);

    // Add $STANDARD_INFORMATION attribute
    NtfsAttrHeader* std_info_attr = (NtfsAttrHeader*)((uint8_t*)record + record->attrs_offset);
    std_info_attr->type = NTFS_ATTR_STANDARD_INFO;
    std_info_attr->non_resident = 0;
    std_info_attr->name_length = 0;
    std_info_attr->length = sizeof(NtfsAttrHeader) + sizeof(NtfsStandardInformation);
    std_info_attr->resident.value_offset = sizeof(NtfsAttrHeader);
    std_info_attr->resident.value_length = sizeof(NtfsStandardInformation);

    NtfsStandardInformation* std_info = (NtfsStandardInformation*)((uint8_t*)std_info_attr + std_info_attr->resident.value_offset);
    std_info->creation_time = RtcGetUnixTime();
    std_info->last_modification_time = std_info->creation_time;
    std_info->last_change_time = std_info->creation_time;
    std_info->last_access_time = std_info->creation_time;
    std_info->file_attributes = 0x20; // ARCHIVE

    // Add $FILE_NAME attribute
    size_t name_len = StringLength(name);
    NtfsAttrHeader* file_name_attr = (NtfsAttrHeader*)((uint8_t*)std_info_attr + std_info_attr->length);
    file_name_attr->type = NTFS_ATTR_FILENAME;
    file_name_attr->non_resident = 0;
    file_name_attr->name_length = 0;
    file_name_attr->length = sizeof(NtfsAttrHeader) + sizeof(NtfsFilename) + name_len * 2;
    file_name_attr->resident.value_offset = sizeof(NtfsAttrHeader);
    file_name_attr->resident.value_length = sizeof(NtfsFilename) + name_len * 2;

    NtfsFilename* file_name = (NtfsFilename*)((uint8_t*)file_name_attr + file_name_attr->resident.value_offset);
    file_name->parent_directory = parent_mft;
    file_name->filename_length = (uint8_t)name_len;
    file_name->filename_namespace = 2; // DOS

    // Convert filename to UTF-16 (ASCII to UCS-2)
    for (size_t i = 0; i < name_len; i++) {
        file_name->filename[i] = (uint16_t)(uint8_t)name[i];
    }

    record->bytes_in_use = (uint32_t)((uint8_t*)file_name_attr + file_name_attr->length - (uint8_t*)record);

    // Write back the MFT record
    uint64_t mft_start_sector = volume.mft_cluster * volume.sectors_per_cluster;
    uint64_t record_byte_offset = mft_record_num * (uint64_t)mft_record_size;
    uint64_t record_sector = mft_start_sector + (record_byte_offset / volume.bytes_per_sector);
    uint32_t sectors_needed = (mft_record_size + volume.bytes_per_sector - 1) / volume.bytes_per_sector;

    if (volume.device->write_blocks(volume.device, record_sector, sectors_needed, record) != 0) {
        KernelFree(record);
        rust_rwlock_write_unlock(volume.lock);
        PrintKernel("NTFS: Failed to write MFT record\n");
        return -1;
    }

    KernelFree(record);
    rust_rwlock_write_unlock(volume.lock);

    return 0;
}

int NtfsCreateDir(const char* path) {
    if (!path || !volume.lock) return -1;

    uint64_t parent_mft = 5;
    char name[256];
    if (NtfsResolveParentAndName(path, &parent_mft, name, sizeof(name)) != 0) return -1;

    rust_rwlock_write_lock(volume.lock, GetCurrentProcess()->pid);

    uint64_t mft_record_num = NtfsAllocateMftRecord();
    if (mft_record_num == 0) {
        PrintKernel("NTFS: No free MFT records\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1; // No free MFT records
    } 

    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1u << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    NtfsMftRecord* record = KernelMemoryAlloc(mft_record_size);
    if (!record) {
        PrintKernel("NTFS: Failed to allocate memory for MFT record\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }
    memset(record, 0, mft_record_size);

    record->signature = 'ELIF';
    record->flags = 2; // Directory
    record->attrs_offset = sizeof(NtfsMftRecord);

    // Add $STANDARD_INFORMATION attribute
    NtfsAttrHeader* std_info_attr = (NtfsAttrHeader*)((uint8_t*)record + record->attrs_offset);
    std_info_attr->type = NTFS_ATTR_STANDARD_INFO;
    std_info_attr->non_resident = 0;
    std_info_attr->length = sizeof(NtfsAttrHeader) + sizeof(NtfsStandardInformation);
    std_info_attr->resident.value_offset = sizeof(NtfsAttrHeader);
    std_info_attr->resident.value_length = sizeof(NtfsStandardInformation);

    NtfsStandardInformation* std_info = (NtfsStandardInformation*)((uint8_t*)std_info_attr + std_info_attr->resident.value_offset);
    uint64_t now = RtcGetUnixTime();
    std_info->creation_time = now;
    std_info->last_modification_time = now;
    std_info->last_change_time = now;
    std_info->last_access_time = now;
    std_info->file_attributes = 0x10; // DIRECTORY

    // Add $FILE_NAME attribute
    size_t name_len = StringLength(name);
    NtfsAttrHeader* file_name_attr = (NtfsAttrHeader*)((uint8_t*)std_info_attr + std_info_attr->length);
    file_name_attr->type = NTFS_ATTR_FILENAME;
    file_name_attr->non_resident = 0;
    file_name_attr->length = sizeof(NtfsAttrHeader) + sizeof(NtfsFilename) + name_len * 2;
    file_name_attr->resident.value_offset = sizeof(NtfsAttrHeader);
    file_name_attr->resident.value_length = sizeof(NtfsFilename) + name_len * 2;

    NtfsFilename* file_name = (NtfsFilename*)((uint8_t*)file_name_attr + file_name_attr->resident.value_offset);
    file_name->parent_directory = parent_mft;
    file_name->filename_length = (uint8_t)name_len;
    file_name->filename_namespace = 2; // DOS
    for (size_t i = 0; i < name_len; i++) {
        file_name->filename[i] = (uint16_t)(uint8_t)name[i];
    }

    // Add $INDEX_ROOT attribute for the directory (minimal placeholder)
    NtfsAttrHeader* index_root_attr = (NtfsAttrHeader*)((uint8_t*)file_name_attr + file_name_attr->length);
    index_root_attr->type = NTFS_ATTR_INDEX_ROOT;
    index_root_attr->non_resident = 0;
    index_root_attr->length = sizeof(NtfsAttrHeader) + sizeof(NtfsIndexRoot);
    index_root_attr->resident.value_offset = sizeof(NtfsAttrHeader);
    index_root_attr->resident.value_length = sizeof(NtfsIndexRoot);

    NtfsIndexRoot* index_root = (NtfsIndexRoot*)((uint8_t*)index_root_attr + index_root_attr->resident.value_offset);
    index_root->type = 0;
    index_root->collation_rule = 0;
    index_root->bytes_per_index_record = 4096;
    index_root->clusters_per_index_record = 1;

    record->bytes_in_use = (uint32_t)((uint8_t*)index_root_attr + index_root_attr->length - (uint8_t*)record);

    uint64_t mft_start_sector = volume.mft_cluster * volume.sectors_per_cluster;
    uint64_t record_byte_offset = mft_record_num * (uint64_t)mft_record_size;
    uint64_t record_sector = mft_start_sector + (record_byte_offset / volume.bytes_per_sector);
    uint32_t sectors_needed = (mft_record_size + volume.bytes_per_sector - 1) / volume.bytes_per_sector;

    if (volume.device->write_blocks(volume.device, record_sector, sectors_needed, record) != 0) {
        KernelFree(record);
        PrintKernel("NTFS: Failed to write MFT record\n");
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    KernelFree(record);
    rust_rwlock_write_unlock(volume.lock);

    return 0;
}

int NtfsDelete(const char* path) {
    if (!path || !volume.lock) return -1;

    uint64_t mft_record_num = NtfsPathToMftRecord(path);
    if (mft_record_num == 0) return -1;

    rust_rwlock_write_lock(volume.lock, GetCurrentProcess()->pid);

    NtfsMftRecord* record = KernelMemoryAlloc(1024);
    if (!record) {
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    if (NtfsReadMftRecord(mft_record_num, record) != 0) {
        KernelFree(record);
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    // Mark MFT record as not in use
    record->flags = 0;

    uint64_t mft_offset = volume.mft_cluster * volume.sectors_per_cluster;
    uint64_t record_offset = mft_offset + (mft_record_num * 1024 / volume.bytes_per_sector);
    if (volume.device->write_blocks(volume.device, record_offset, 2, record) != 0) {
        KernelFree(record);
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    KernelFree(record);

    // Clear the bit in the $MFT::$BITMAP (MFT allocation bitmap)
    uint32_t mft_record_size;
    if (volume.boot_sector.clusters_per_file_record > 0) {
        mft_record_size = volume.boot_sector.clusters_per_file_record * volume.bytes_per_cluster;
    } else {
        mft_record_size = 1 << (-(int8_t)volume.boot_sector.clusters_per_file_record);
    }

    NtfsMftRecord* mft0 = KernelMemoryAlloc(mft_record_size);
    if (!mft0) {
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    if (NtfsReadMftRecord(0, mft0) != 0) {
        KernelFree(mft0);
        rust_rwlock_write_unlock(volume.lock);
        return -1;
    }

    uint8_t* attr_ptr = (uint8_t*)mft0 + mft0->attrs_offset;
    uint8_t* record_end = (uint8_t*)mft0 + mft_record_size;

    while (attr_ptr + sizeof(NtfsAttrHeader) <= record_end &&
           attr_ptr < (uint8_t*)mft0 + mft0->bytes_in_use) {
        NtfsAttrHeader* attr = (NtfsAttrHeader*)attr_ptr;

        if (attr->length == 0 || attr->length < sizeof(NtfsAttrHeader)) break;
        if (attr_ptr + attr->length > record_end) break;

        if (attr->type == NTFS_ATTR_BITMAP) {
            if (!attr->non_resident) {
                uint8_t* bitmap_data = attr_ptr + attr->resident.value_offset;
                uint32_t byte_index = mft_record_num / 8;
                uint32_t bit_index = mft_record_num % 8;
                bitmap_data[byte_index] &= ~(1 << bit_index);

                uint64_t mft_start_sector = volume.mft_cluster * volume.sectors_per_cluster;
                uint64_t record_byte_offset = 0 * mft_record_size;
                uint64_t record_sector = mft_start_sector + (record_byte_offset / volume.bytes_per_sector);
                uint32_t sectors_needed = (mft_record_size + volume.bytes_per_sector - 1) / volume.bytes_per_sector;
                volume.device->write_blocks(volume.device, record_sector, sectors_needed, mft0);
            } else {
                // Minimal support for clearing bit in non-resident $MFT::$BITMAP (first run only)
                int64_t lcn = -1;
                uint64_t run_clusters = 0;
                if (NtfsParseFirstRun(attr, &lcn, &run_clusters) != 0 || lcn < 0 || run_clusters == 0) {
                    PrintKernel("NTFS: $MFT::$BITMAP clear-bit unsupported mapping pairs\n");
                    break;
                }
                uint64_t total_bytes = run_clusters * (uint64_t)volume.bytes_per_cluster;
                if (attr->nonresident.data_size < total_bytes) total_bytes = attr->nonresident.data_size;
                if (total_bytes == 0) {
                    PrintKernel("NTFS: $MFT::$BITMAP non-resident empty while clearing\n");
                    break;
                }
                uint32_t sectors = (uint32_t)((total_bytes + volume.bytes_per_sector - 1) / volume.bytes_per_sector);
                uint8_t* bitmap_buf = KernelMemoryAlloc((size_t)sectors * volume.bytes_per_sector);
                if (!bitmap_buf) {
                    PrintKernel("NTFS: Failed to alloc bitmap buffer (clear)\n");
                    break;
                }
                uint64_t start_sector = (uint64_t)lcn * volume.sectors_per_cluster;
                if (volume.device->read_blocks(volume.device, start_sector, sectors, bitmap_buf) != 0) {
                    PrintKernel("NTFS: Failed to read non-resident $MFT::$BITMAP (clear)\n");
                    KernelFree(bitmap_buf);
                    break;
                }
                uint64_t byte_index = mft_record_num / 8;
                uint32_t bit_index = (uint32_t)(mft_record_num % 8);
                uint64_t available_bytes = (uint64_t)sectors * volume.bytes_per_sector;
                if (byte_index < available_bytes) {
                    bitmap_buf[byte_index] &= (uint8_t)~(1u << bit_index);
                    if (volume.device->write_blocks(volume.device, start_sector, sectors, bitmap_buf) != 0) {
                        PrintKernel("NTFS: Failed to write non-resident $MFT::$BITMAP (clear)\n");
                    }
                } else {
                    PrintKernel("NTFS: Clear target outside first run (unsupported)\n");
                }
                KernelFree(bitmap_buf);
            }
            break;
        }
        attr_ptr += attr->length;
    }

    KernelFree(mft0);
    rust_rwlock_write_unlock(volume.lock);

    return 0;
}