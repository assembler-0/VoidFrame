#pragma once

#include "BlockDevice.h"
#include "stdint.h"

#define MBR_PARTITION_COUNT 4

// MBR Partition Entry
#pragma pack(push, 1)
typedef struct {
    uint8_t status;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t num_sectors;
} MBRPartitionEntry;

// Master Boot Record
typedef struct {
    uint8_t boot_code[446];
    MBRPartitionEntry partitions[MBR_PARTITION_COUNT];
    uint16_t boot_signature;
} MasterBootRecord;
#pragma pack(pop)

void ParseMBR(BlockDevice* device);
