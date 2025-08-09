#pragma once

#include <stdint.h>

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
} Fat12BootSector;

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
} Fat12DirEntry;

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
    uint8_t drive;
    Fat12BootSector boot;
    uint8_t* fat_table;
    uint32_t fat_sector;
    uint32_t root_sector;
    uint32_t data_sector;
} Fat12Volume;

// Core Functions
int Fat12Init(uint8_t drive);
int Fat12ReadFile(const char* filename, void* buffer, uint32_t max_size);
int Fat12ListRoot(void);
int Fat12GetCluster(uint16_t cluster, uint8_t* buffer);