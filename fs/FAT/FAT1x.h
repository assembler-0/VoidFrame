#pragma once

#include <stdint.h>

#include "BlockDevice.h"

// FAT12 Boot Sector Structure
typedef struct __attribute__((packed)) {
    uint8_t  jump[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} Fat1xBootSector;

// FAT12 Directory Entry
typedef struct __attribute__((packed)) {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t cluster_low;
    uint32_t file_size;
} Fat1xDirEntry;

// Attributes
#define FAT12_ATTR_READ_ONLY  0x01
#define FAT12_ATTR_HIDDEN     0x02
#define FAT12_ATTR_SYSTEM     0x04
#define FAT12_ATTR_VOLUME_ID  0x08
#define FAT12_ATTR_DIRECTORY  0x10
#define FAT12_ATTR_ARCHIVE    0x20

// Special cluster values
#define FAT12_CLUSTER_FREE    0x000
#define FAT12_CLUSTER_EOF     0xFF8

typedef struct {
    BlockDevice* device;
    Fat1xBootSector boot;
    uint8_t* fat_table;
    uint32_t fat_sector;
    uint32_t root_sector;
    uint32_t data_sector;
} Fat1xVolume;

// Core Functions
int Fat1xMount(BlockDevice* device, const char* mount_point);
int Fat1xDetect(BlockDevice* device);
int Fat1xReadFile(const char* filename, void* buffer, uint32_t max_size);
int Fat1xWriteFile(const char* filename, const void* buffer, uint32_t size);
int Fat1xDeleteFile(const char* filename);
int Fat1xCreateFile(const char* filename);
int Fat1xDeleteRecursive(const char* path);
int Fat1xCreateDir(const char* dirname);
int Fat1xListRoot(void);
int Fat1xGetCluster(uint16_t cluster, uint8_t* buffer);
int Fat1xIsDirectory(const char* path);
int Fat1xListDirectory(const char* path);
uint64_t Fat1xGetFileSize(const char* path);