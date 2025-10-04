#include "BlockDevice.h"
#include "MBR.h"
#include "StringOps.h"
#include "Console.h"

static BlockDevice g_block_devices[MAX_BLOCK_DEVICES];
static int g_next_device_id = 0;

void BlockDeviceInit() {
    PrintKernel("BlockDevice: Initializing block device subsystem...\n");
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        g_block_devices[i].active = false;
    }
    g_next_device_id = 0;
    PrintKernel("BlockDevice: Block device table cleared\n");
}

BlockDevice* BlockDeviceRegister(BlockDeviceType type, uint32_t block_size, uint64_t total_blocks, const char* name, void* driver_data, ReadBlocksFunc read, WriteBlocksFunc write) {
    PrintKernel("BlockDevice: Registering device '");
    PrintKernel(name);
    PrintKernel("' (type=");
    PrintKernelInt(type);
    PrintKernel(", blocks=");
    PrintKernelInt(total_blocks);
    PrintKernel(")\n");
    
    if (g_next_device_id >= MAX_BLOCK_DEVICES) {
        PrintKernel("BlockDevice: Maximum number of block devices reached\n");
        return NULL; // No more space for new devices
    }

    int id = g_next_device_id++;
    BlockDevice* dev = &g_block_devices[id];

    dev->id = id;
    dev->active = true;
    dev->type = type;
    dev->block_size = block_size;
    dev->total_blocks = total_blocks;
    FastStrCopy(dev->name, name, sizeof(dev->name) - 1);
    dev->driver_data = driver_data;
    dev->read_blocks = read;
    dev->write_blocks = write;
    dev->parent = NULL;
    dev->lba_offset = 0;

    PrintKernel("BlockDevice: Successfully registered '");
    PrintKernel(name);
    PrintKernel("' as device ID ");
    PrintKernelInt(id);
    PrintKernel("\n");

    return dev;
}

BlockDevice* BlockDeviceGet(int id) {
    if (id < 0 || id >= g_next_device_id || !g_block_devices[id].active) {
        return NULL;
    }
    return &g_block_devices[id];
}

int BlockDeviceRead(int device_id, uint64_t start_lba, uint32_t count, void* buffer) {
    BlockDevice* dev = BlockDeviceGet(device_id);
    if (!dev || !dev->read_blocks) {
        return -1;
    }
    return dev->read_blocks(dev, start_lba, count, buffer);
}

int BlockDeviceWrite(int device_id, uint64_t start_lba, uint32_t count, const void* buffer) {
    BlockDevice* dev = BlockDeviceGet(device_id);
    if (!dev || !dev->write_blocks) {
        return -1;
    }
    return dev->write_blocks(dev, start_lba, count, buffer);
}

void BlockDeviceDetectAndRegisterPartitions(BlockDevice* drive) {
    if (!drive || drive->type == DEVICE_TYPE_PARTITION) {
        return; // Don't partition a partition
    }
    ParseMBR(drive);
}

void BlockDevicePrint(const char* args) {
    (void)args;
    PrintKernel("BlockDevice: Registered devices:\n");
    for (int i = 0; i < g_next_device_id; i++) {
        BlockDevice* dev = &g_block_devices[i];
        if (!dev) continue;
        PrintKernel("BlockDevice: ID=");
        PrintKernelInt(dev->id);
        PrintKernel(", type=");
        PrintKernelInt(dev->type);
        PrintKernel(", blocks=");
        PrintKernelInt(dev->total_blocks);
        PrintKernel(", name=");
        PrintKernel(dev->name);
        PrintKernel("\n");
    }
}

BlockDevice* SearchBlockDevice(const char* name) {
    for (int i = 0; i < g_next_device_id; i++) {
        BlockDevice* dev = &g_block_devices[i];
        if (!dev) continue;
        if (FastStrCmp(dev->name, name) == 0) return dev;
    }
    return NULL;
}