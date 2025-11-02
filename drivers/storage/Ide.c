#include <Ide.h>
#include <../APIC/APIC.h>
#include <BlockDevice.h>
#include <Console.h>
#include <DriveNaming.h>
#include <Format.h>
#include <Io.h>
#include <MemOps.h>
#include <SpinlockRust.h>

static IdeChannel channels[2];
static RustSpinLock* ide_lock = NULL;

// Wait for drive to be ready (not busy)
static int IdeWaitReady(uint16_t base_port) {
    uint32_t timeout = 500000;  // Increased timeout for QEMU

    while (timeout--) {
        uint8_t status = inb(base_port + IDE_REG_STATUS);
        if (!(status & IDE_STATUS_BSY)) {
            if (status & IDE_STATUS_ERR) return IDE_ERROR_IO;
            return IDE_OK;  // Don't require RDY bit
        }
        // Small delay
        for (volatile int i = 0; i < 100; i++) {}
    }
    return IDE_ERROR_TIMEOUT;
}

// Wait for data ready
static int IdeWaitData(uint16_t base_port) {
    uint32_t timeout = 500000;  // Increased timeout

    while (timeout--) {
        uint8_t status = inb(base_port + IDE_REG_STATUS);
        if (!(status & IDE_STATUS_BSY) && (status & IDE_STATUS_DRQ)) {
            return IDE_OK;
        }
        if (status & IDE_STATUS_ERR) {
            return IDE_ERROR_IO;
        }
        // Small delay
        for (volatile int i = 0; i < 100; i++);
    }
    return IDE_ERROR_TIMEOUT;
}

// Select drive and set LBA
static int IdeSelectDrive(uint16_t base_port, uint8_t drive, uint32_t lba) {
    // Wait for controller ready
    int result = IdeWaitReady(base_port);
    if (result != IDE_OK) return result;

    // Select drive with LBA mode and upper 4 bits of LBA
    uint8_t drive_head = 0xE0 | (drive << 4) | ((lba >> 24) & 0x0F);
    outb(base_port + IDE_REG_DRIVE_HEAD, drive_head);

    // Small delay after drive selection
    for (int i = 0; i < 4; i++) {
        inb(base_port + IDE_REG_STATUS);
    }

    return IdeWaitReady(base_port);
}

// Identify drive and get model string
static int IdeIdentifyDrive(uint16_t base_port, uint8_t drive, uint16_t* buffer, uint64_t* total_sectors) {
    int result = IdeSelectDrive(base_port, drive, 0);
    if (result != IDE_OK) return result;

    // Try ATA IDENTIFY (0xEC) first
    outb(base_port + IDE_REG_COMMAND, IDE_CMD_IDENTIFY);

    // Small delay after command
    for (volatile int i = 0; i < 1000; i++) {}

    uint8_t status = inb(base_port + IDE_REG_STATUS);
    if (status != 0 && status != 0xFF) {
        // Wait for data; if successful, read identify data
        result = IdeWaitData(base_port);
        if (result == IDE_OK) {
            for (int i = 0; i < 256; i++) {
                buffer[i] = inw(base_port + IDE_REG_DATA);
            }
            // LBA28 total sectors at words 60-61
            *total_sectors = *(uint32_t*)(buffer + 60);
            return IDE_OK;
        }
    }

    // Fallback: IDENTIFY PACKET DEVICE (0xA1) for ATAPI (e.g., CD-ROM)
    result = IdeSelectDrive(base_port, drive, 0);
    if (result != IDE_OK) return result;

    outb(base_port + IDE_REG_COMMAND, IDE_CMD_IDENTIFY_PACKET);

    // Wait a bit after command
    for (volatile int i = 0; i < 1000; i++) {}

    // Re-read status; if still no device, bail out
    status = inb(base_port + IDE_REG_STATUS);
    if (status == 0 || status == 0xFF) return IDE_ERROR_NO_DRIVE;

    result = IdeWaitData(base_port);
    if (result != IDE_OK) return result;

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(base_port + IDE_REG_DATA);
    }
    *total_sectors = 0; // ATAPI size is more complex

    return IDE_OK;
}

int IdeInit(void) {
    if (!ide_lock) {
        ide_lock = rust_spinlock_new();
        if (!ide_lock) {
            PrintKernelError("IDE: Failed to create spinlock\n");
            return IDE_ERROR_IO;
        }
    }
    PrintKernel("IDE: Initializing IDE controller...\n");

    // Initialize channel structures
    channels[0].base_port = IDE_PRIMARY_BASE;
    channels[0].ctrl_port = IDE_PRIMARY_CTRL;
    channels[1].base_port = IDE_SECONDARY_BASE;
    channels[1].ctrl_port = IDE_SECONDARY_CTRL;

    uint16_t identify_buffer[256];
    int drives_found = 0;

    // Check each channel and drive
    for (int channel = 0; channel < 2; channel++) {
        PrintKernel("IDE: Checking channel ");
        PrintKernelInt(channel);
        PrintKernel(" (base=0x");
        PrintKernelHex(channels[channel].base_port);
        PrintKernel(")\n");
        
        for (int drive = 0; drive < 2; drive++) {
            PrintKernel("IDE: Probing channel ");
            PrintKernelInt(channel);
            PrintKernel(", drive ");
            PrintKernelInt(drive);
            PrintKernel("...\n");
            
            channels[channel].drive_exists[drive] = 0;
            channels[channel].is_atapi[drive] = 0;

            uint64_t total_sectors;
            int result = IdeIdentifyDrive(channels[channel].base_port, drive, identify_buffer, &total_sectors);
            PrintKernel("IDE: Identify result: ");
            PrintKernelInt(result);
            PrintKernel("\n");
            
            if (result == IDE_OK) {
                PrintKernel("IDE: Drive found on channel ");
                PrintKernelInt(channel);
                PrintKernel(", drive ");
                PrintKernelInt(drive);
                PrintKernel("\n");
                
                channels[channel].drive_exists[drive] = 1;
                drives_found++;

                // Check for ATAPI
                if (identify_buffer[0] & 0x8000) {
                    channels[channel].is_atapi[drive] = 1;
                }

                // Extract model string (words 27-46, byte-swapped)
                char* model = channels[channel].model[drive];
                for (int i = 0; i < 20; i++) {
                    uint16_t word = identify_buffer[27 + i];
                    model[i * 2] = (word >> 8) & 0xFF;
                    model[i * 2 + 1] = word & 0xFF;
                }
                model[40] = '\0';

                // Trim trailing spaces
                for (int i = 39; i >= 0 && model[i] == ' '; i--) {
                    model[i] = '\0';
                }

                char dev_name[16];
                GenerateDriveNameInto(DEVICE_TYPE_IDE, dev_name);

                BlockDevice* dev = BlockDeviceRegister(
                    DEVICE_TYPE_IDE,
                    512,
                    total_sectors,
                    dev_name,
                    (void*)(uintptr_t)(channel * 2 + drive + 1),
                    (ReadBlocksFunc)IdeReadBlocks,
                    (WriteBlocksFunc)IdeWriteBlocks
                );

                if (dev) BlockDeviceDetectAndRegisterPartitions(dev);
            }
        }
    }

    if (drives_found == 0) {
        PrintKernelWarning("IDE: No IDE drives detected\n");
        return IDE_ERROR_NO_DRIVE;
    }

    PrintKernelSuccess("IDE: Controller initialized, ");
    PrintKernelInt(drives_found);
    PrintKernel(" drive(s) found\n");

    PrintKernel("Unmasking IDE IRQs\n");
    ApicEnableIrq(12);
    ApicEnableIrq(14);
    PrintKernelSuccess("IDE IRQs unmasked\n");
    return IDE_OK;
}

int IdeReadBlocks(BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer) {
    if (!device || !device->driver_data) {
        PrintKernel("IDE: Invalid device or driver_data\n");
        return -1;
    }
    uint8_t drive = (uintptr_t)device->driver_data - 1;

    uint8_t channel = drive / 2;
    uint8_t drive_num = drive % 2;


    if (!channels[channel].drive_exists[drive_num]) {
        PrintKernel("IDE: Drive does not exist\n");
        return IDE_ERROR_NO_DRIVE;
    }

    rust_spinlock_lock(ide_lock);
    const uint16_t base_port = channels[channel].base_port;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t lba = start_lba + i;
        uint8_t* buf = (uint8_t*)buffer + (i * 512);

        int result = IdeSelectDrive(base_port, drive_num, lba);
        if (result != IDE_OK) {
            rust_spinlock_unlock(ide_lock);
            return result;
        }

        outb(base_port + IDE_REG_SECTOR_COUNT, 1);
        outb(base_port + IDE_REG_LBA_LOW, lba & 0xFF);
        outb(base_port + IDE_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(base_port + IDE_REG_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(base_port + IDE_REG_COMMAND, IDE_CMD_READ_SECTORS);

        result = IdeWaitData(base_port);
        if (result != IDE_OK) {
            PrintKernel("IDE: Wait for data failed with error ");
            PrintKernelInt(result);
            PrintKernel("\n");
            rust_spinlock_unlock(ide_lock);
            return result;
        }

        uint16_t* buf16 = (uint16_t*)buf;
        for (int j = 0; j < 256; j++) {
            buf16[j] = inw(base_port + IDE_REG_DATA);
        }
    }

    rust_spinlock_unlock(ide_lock);
    return 0;
}

int IdeWriteBlocks(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer) {
    if (!device || !device->driver_data) return -1;
    uint8_t drive = (uintptr_t)device->driver_data - 1;

    uint8_t channel = drive / 2;
    uint8_t drive_num = drive % 2;

    if (!channels[channel].drive_exists[drive_num]) {
        return IDE_ERROR_NO_DRIVE;
    }

    rust_spinlock_lock(ide_lock);
    uint16_t base_port = channels[channel].base_port;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t lba = start_lba + i;
        const uint8_t* buf = (const uint8_t*)buffer + (i * 512);

        int result = IdeSelectDrive(base_port, drive_num, lba);
        if (result != IDE_OK) {
            rust_spinlock_unlock(ide_lock);
            return result;
        }

        outb(base_port + IDE_REG_SECTOR_COUNT, 1);
        outb(base_port + IDE_REG_LBA_LOW, lba & 0xFF);
        outb(base_port + IDE_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(base_port + IDE_REG_LBA_HIGH, (lba >> 16) & 0xFF);
        outb(base_port + IDE_REG_COMMAND, IDE_CMD_WRITE_SECTORS);

        result = IdeWaitData(base_port);
        if (result != IDE_OK) {
            rust_spinlock_unlock(ide_lock);
            return result;
        }

        const uint16_t* buf16 = (const uint16_t*)buf;
        for (int j = 0; j < 256; j++) {
            outw(base_port + IDE_REG_DATA, buf16[j]);
        }

        result = IdeWaitReady(base_port);
        if (result != IDE_OK) {
            rust_spinlock_unlock(ide_lock);
            return result;
        }
    }

    rust_spinlock_unlock(ide_lock);
    return 0;
}

int IdeGetDriveInfo(uint8_t drive, char* model_out) {
    if (drive >= 4) return IDE_ERROR_NO_DRIVE;

    uint8_t channel = drive / 2;
    uint8_t drive_num = drive % 2;

    if (!channels[channel].drive_exists[drive_num]) {
        return IDE_ERROR_NO_DRIVE;
    }

    if (model_out) {
        FastMemcpy(model_out, channels[channel].model[drive_num], 41);
    }

    return IDE_OK;
}

// IDE interrupt handlers - just acknowledge for now
void IDEPrimaryIRQH(void) {
    // Primary IDE channel interrupt
    // Read status to acknowledge
    inb(IDE_PRIMARY_BASE + IDE_REG_STATUS);
}

void IDESecondaryIRQH(void) {
    // Secondary IDE channel interrupt
    // Read status to acknowledge
    inb(IDE_SECONDARY_BASE + IDE_REG_STATUS);
}

int IdeReadLBA2048(uint8_t drive, uint32_t lba, void* buffer) {
    if (drive >= 4) return IDE_ERROR_NO_DRIVE;

    uint8_t channel = drive / 2;
    uint8_t drive_num = drive % 2;

    if (!channels[channel].drive_exists[drive_num] || !channels[channel].is_atapi[drive_num]) {
        return IDE_ERROR_NO_DRIVE;
    }

    rust_spinlock_lock(ide_lock);
    uint16_t base_port = channels[channel].base_port;
    rust_spinlock_unlock(ide_lock);
    int result;

    result = IdeSelectDrive(base_port, drive_num, 0); // LBA is in the packet
    if (result != IDE_OK) {
        return result;
    }

    uint8_t atapi_packet[12] = {
        ATAPI_CMD_READ_10, 0,
        (lba >> 24) & 0xFF, (lba >> 16) & 0xFF, (lba >> 8) & 0xFF, lba & 0xFF,
        0, 0, 1, // 1 sector
        0, 0, 0
    };

    outb(base_port + IDE_REG_FEATURES, 0); // PIO mode
    outb(base_port + IDE_REG_LBA_MID, 2048 & 0xFF);
    outb(base_port + IDE_REG_LBA_HIGH, 2048 >> 8);

    outb(base_port + IDE_REG_COMMAND, IDE_CMD_PACKET);

    result = IdeWaitData(base_port);
    if (result != IDE_OK) {
        return result;
    }

    // Send packet
    uint16_t* packet_ptr = (uint16_t*)atapi_packet;
    for (int i = 0; i < 6; i++) {
        outw(base_port + IDE_REG_DATA, packet_ptr[i]);
    }

    result = IdeWaitData(base_port);
    if (result != IDE_OK) {
        return result;
    }

    // Read data
    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 1024; i++) { // 2048 bytes = 1024 words
        buf16[i] = inw(base_port + IDE_REG_DATA);
    }

    return IDE_OK;
}