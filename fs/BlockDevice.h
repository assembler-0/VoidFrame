#pragma once

#include "stdint.h"
#include "stdbool.h"

#define MAX_BLOCK_DEVICES 16

typedef enum {
    DEVICE_TYPE_UNKNOWN,
    DEVICE_TYPE_IDE,
    DEVICE_TYPE_AHCI,
    DEVICE_TYPE_NVME,
    DEVICE_TYPE_USB,
    DEVICE_TYPE_VIRTIO,
    DEVICE_TYPE_PARTITION
} BlockDeviceType;

struct BlockDevice;

typedef int (*ReadBlocksFunc)(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer);
typedef int (*WriteBlocksFunc)(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer);

typedef struct BlockDevice {
    int id;
    bool active;
    BlockDeviceType type;
    uint32_t block_size;
    uint64_t total_blocks;
    char name[32];

    // Driver-specific data
    void* driver_data;

    // Parent device (for partitions)
    struct BlockDevice* parent;
    uint64_t lba_offset; // for partitions

    // Function pointers for I/O
    ReadBlocksFunc read_blocks;
    WriteBlocksFunc write_blocks;
} BlockDevice;

void BlockDeviceInit();
BlockDevice* BlockDeviceRegister(BlockDeviceType type, uint32_t block_size, uint64_t total_blocks, const char* name, void* driver_data, ReadBlocksFunc read, WriteBlocksFunc write);
BlockDevice* BlockDeviceGet(int id);
int BlockDeviceRead(int device_id, uint64_t start_lba, uint32_t count, void* buffer);
int BlockDeviceWrite(int device_id, uint64_t start_lba, uint32_t count, const void* buffer);
void BlockDeviceDetectAndRegisterPartitions(BlockDevice* drive);
