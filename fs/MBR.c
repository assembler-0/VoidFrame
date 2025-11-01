#include "MBR.h"
#include "BlockDevice.h"
#include "Console.h"
#include "Format.h"
#include "KernelHeap.h"

static int PartitionReadBlocks(BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer) {
    if (!device || !device->parent) {
        return -1;
    }
    return device->parent->read_blocks(device->parent, device->lba_offset + start_lba, count, buffer);
}

static int PartitionWriteBlocks(BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer) {
    if (!device || !device->parent) {
        return -1;
    }
    return device->parent->write_blocks(device->parent, device->lba_offset + start_lba, count, buffer);
}

void ParseMBR(BlockDevice* device) {
    PrintKernel("MBR: Attempting to parse MBR for device ");
    PrintKernel(device->name);
    PrintKernel(" (id=");
    PrintKernelInt(device->id);
    PrintKernel(")\n");

    MasterBootRecord* mbr = (MasterBootRecord*)KernelMemoryAlloc(device->block_size);
    if (!mbr) {
        PrintKernelError("MBR: Failed to allocate memory for MBR\n");
        return;
    }

    PrintKernel("MBR: Reading sector 0 from device...\n");
    int read_result = BlockDeviceRead(device->id, 0, 1, mbr);
    if (read_result != 0) {
        PrintKernelError("MBR: Failed to read MBR from device ");
        PrintKernel(device->name);
        PrintKernel(" (error code: ");
        PrintKernelInt(read_result);
        PrintKernel(")\n");
        KernelFree(mbr);
        return;
    }

    PrintKernel("MBR: Successfully read sector, checking signature...\n");
    PrintKernel("MBR: Boot signature = 0x");
    PrintKernelHex(mbr->boot_signature);
    PrintKernel("\n");

    if (mbr->boot_signature != 0xAA55) {
        PrintKernel("MBR: No MBR found on device ");
        PrintKernel(device->name);
        PrintKernel(" (invalid signature)\n");
        KernelFree(mbr);
        return;
    }

    PrintKernel("MBR: Valid MBR found, parsing partitions...\n");

    for (int i = 0; i < MBR_PARTITION_COUNT; i++) {
        MBRPartitionEntry* p = &mbr->partitions[i];
        PrintKernel("MBR: Partition ");
        PrintKernelInt(i);
        PrintKernel(": type=0x");
        PrintKernelHex(p->type);
        PrintKernel(", sectors=");
        PrintKernelInt(p->num_sectors);
        PrintKernel(", start_lba=");
        PrintKernelInt(p->lba_start);
        PrintKernel("\n");
        
        if (p->type != 0 && p->num_sectors > 0) {
            char part_name[32];
            snprintf(part_name, sizeof(part_name), "%s-p%d", device->name, i + 1);

            PrintKernel("MBR: Registering partition ");
            PrintKernel(part_name);
            PrintKernel("\n");

            BlockDevice* part_dev = BlockDeviceRegister(
                DEVICE_TYPE_PARTITION,
                device->block_size,
                p->num_sectors,
                part_name,
                NULL, // No special driver data
                PartitionReadBlocks,
                PartitionWriteBlocks
            );

            if (part_dev) {
                part_dev->parent = device;
                part_dev->lba_offset = p->lba_start;
                PrintKernel("MBR: Partition ");
                PrintKernel(part_name);
                PrintKernel(" registered successfully\n");
            } else {
                PrintKernel("MBR: Failed to register partition ");
                PrintKernel(part_name);
                PrintKernel("\n");
            }
        }
    }

    KernelFree(mbr);
}
