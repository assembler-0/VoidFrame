#pragma once
#include "BlockDevice.h"
#include "stdint.h"
#include "KernelHeap.h"

// NTFS Boot Sector
typedef struct {
    uint8_t  jump[3];
    char     oem_id[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  unused1[3];
    uint16_t unused2;
    uint8_t  media_descriptor;
    uint16_t unused3;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t unused4;
    uint32_t unused5;
    uint64_t total_sectors;
    uint64_t mft_cluster;
    uint64_t mft_mirror_cluster;
    int8_t   clusters_per_file_record;
    uint8_t  unused6[3];
    int8_t   clusters_per_index_buffer;
    uint8_t  unused7[3];
    uint64_t volume_serial;
    uint32_t checksum;
    uint8_t  bootstrap[426];
    uint16_t signature;
} __attribute__((packed)) NtfsBootSector;

// MFT Record Header
typedef struct {
    uint32_t signature;
    uint16_t update_seq_offset;
    uint16_t update_seq_size;
    uint64_t lsn;
    uint16_t sequence_number;
    uint16_t link_count;
    uint16_t attrs_offset;
    uint16_t flags;
    uint32_t bytes_in_use;
    uint32_t bytes_allocated;
    uint64_t base_mft_record;
    uint16_t next_attr_instance;
    uint16_t reserved;
    uint32_t mft_record_number;
} __attribute__((packed)) NtfsMftRecord;

// Attribute Header
typedef struct {
    uint32_t type;
    uint32_t length;
    uint8_t  non_resident;
    uint8_t  name_length;
    uint16_t name_offset;
    uint16_t flags;
    uint16_t instance;
    union {
        struct {
            uint32_t value_length;
            uint16_t value_offset;
            uint8_t  flags;
            uint8_t  reserved;
        } resident;
        struct {
            uint64_t lowest_vcn;
            uint64_t highest_vcn;
            uint16_t mapping_pairs_offset;
            uint8_t  compression_unit;
            uint8_t  reserved[5];
            uint64_t allocated_size;
            uint64_t data_size;
            uint64_t initialized_size;
        } nonresident;
    };
} __attribute__((packed)) NtfsAttrHeader;

// Attribute Types
#define NTFS_ATTR_STANDARD_INFO     0x10
#define NTFS_ATTR_FILENAME          0x30
#define NTFS_ATTR_DATA              0x80
#define NTFS_ATTR_INDEX_ROOT        0x90
#define NTFS_ATTR_INDEX_ALLOCATION  0xA0
#define NTFS_ATTR_BITMAP            0xB0

typedef struct {
    uint64_t creation_time;
    uint64_t last_modification_time;
    uint64_t last_change_time;
    uint64_t last_access_time;
    uint32_t file_attributes;
    uint32_t reserved;
} __attribute__((packed)) NtfsStandardInformation;

// Index Root Attribute
typedef struct {
    uint32_t type;
    uint32_t collation_rule;
    uint32_t bytes_per_index_record;
    uint8_t clusters_per_index_record;
    uint8_t reserved[3];
} __attribute__((packed)) NtfsIndexRoot;

// Index Entry
typedef struct {
    uint64_t mft_record_number;
    uint16_t length;
    uint16_t file_name_length;
    uint32_t flags;
    uint16_t file_name[1];
} __attribute__((packed)) NtfsIndexEntry;

// File Name Attribute
typedef struct {
    uint64_t parent_directory;
    uint64_t creation_time;
    uint64_t last_modification_time;
    uint64_t last_change_time;
    uint64_t last_access_time;
    uint64_t allocated_size;
    uint64_t data_size;
    uint32_t file_attributes;
    uint32_t reparse_value;
    uint8_t  filename_length;
    uint8_t  filename_namespace;
    uint16_t filename[];
} __attribute__((packed)) NtfsFilename;

// VFS Interface Functions
int NtfsDetect(struct BlockDevice* device);
int NtfsMount(struct BlockDevice* device, const char* mount_point);
int NtfsReadFile(const char* path, void* buffer, uint32_t max_size);
int NtfsWriteFile(const char* path, const void* buffer, uint32_t size);
int NtfsListDir(const char* path);
int NtfsIsFile(const char* path);
int NtfsIsDir(const char* path);
uint64_t NtfsGetFileSize(const char* path);
int NtfsCreateFile(const char* path);
int NtfsCreateDir(const char* path);
int NtfsDelete(const char* path);
void NtfsSetActive(BlockDevice* device);

// Internal Functions
int NtfsReadMftRecord(uint64_t record_num, NtfsMftRecord* record);
uint64_t NtfsPathToMftRecord(const char* path);