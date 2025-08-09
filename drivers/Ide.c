#include "Ide.h"
#include "Console.h"
#include "Io.h"
#include "MemOps.h"
#include "Spinlock.h"

static IdeChannel channels[2];
static volatile int ide_lock = 0;

// Wait for drive to be ready (not busy)
static int IdeWaitReady(uint16_t base_port) {
    uint32_t timeout = 100000;  // ~100ms timeout
    
    while (timeout--) {
        uint8_t status = inb(base_port + IDE_REG_STATUS);
        if (!(status & IDE_STATUS_BSY)) {
            if (status & IDE_STATUS_ERR) {
                return IDE_ERROR_IO;
            }
            return IDE_OK;
        }
    }
    return IDE_ERROR_TIMEOUT;
}

// Wait for data ready
static int IdeWaitData(uint16_t base_port) {
    uint32_t timeout = 100000;
    
    while (timeout--) {
        uint8_t status = inb(base_port + IDE_REG_STATUS);
        if (status & IDE_STATUS_DRQ) {
            return IDE_OK;
        }
        if (status & IDE_STATUS_ERR) {
            return IDE_ERROR_IO;
        }
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
static int IdeIdentifyDrive(uint16_t base_port, uint8_t drive, uint16_t* buffer) {
    int result = IdeSelectDrive(base_port, drive, 0);
    if (result != IDE_OK) return result;
    
    // Send IDENTIFY command
    outb(base_port + IDE_REG_COMMAND, IDE_CMD_IDENTIFY);
    
    // Check if drive exists
    uint8_t status = inb(base_port + IDE_REG_STATUS);
    if (status == 0) return IDE_ERROR_NO_DRIVE;
    
    result = IdeWaitData(base_port);
    if (result != IDE_OK) return result;
    
    // Read 256 words of identify data
    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(base_port + IDE_REG_DATA);
    }
    
    return IDE_OK;
}

int IdeInit(void) {
    PrintKernel("[IDE] Initializing IDE controller...\n");
    
    // Initialize channel structures
    channels[0].base_port = IDE_PRIMARY_BASE;
    channels[0].ctrl_port = IDE_PRIMARY_CTRL;
    channels[1].base_port = IDE_SECONDARY_BASE;
    channels[1].ctrl_port = IDE_SECONDARY_CTRL;
    
    uint16_t identify_buffer[256];
    int drives_found = 0;
    
    // Check each channel and drive
    for (int channel = 0; channel < 2; channel++) {
        for (int drive = 0; drive < 2; drive++) {
            channels[channel].drive_exists[drive] = 0;
            
            int result = IdeIdentifyDrive(channels[channel].base_port, drive, identify_buffer);
            if (result == IDE_OK) {
                channels[channel].drive_exists[drive] = 1;
                drives_found++;
                
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
                
                PrintKernel("[IDE] Drive ");
                PrintKernelInt(channel * 2 + drive);
                PrintKernel(": ");
                PrintKernel(model);
                PrintKernel("\n");
            }
        }
    }
    
    if (drives_found == 0) {
        PrintKernelWarning("[IDE] No IDE drives detected\n");
        return IDE_ERROR_NO_DRIVE;
    }
    
    PrintKernelSuccess("[IDE] Controller initialized, ");
    PrintKernelInt(drives_found);
    PrintKernel(" drive(s) found\n");
    return IDE_OK;
}

int IdeReadSector(uint8_t drive, uint32_t lba, void* buffer) {
    if (drive >= 4) return IDE_ERROR_NO_DRIVE;
    
    uint8_t channel = drive / 2;
    uint8_t drive_num = drive % 2;
    
    if (!channels[channel].drive_exists[drive_num]) {
        return IDE_ERROR_NO_DRIVE;
    }
    
    irq_flags_t flags = SpinLockIrqSave(&ide_lock);
    
    uint16_t base_port = channels[channel].base_port;
    int result;
    
    // Select drive and set LBA
    result = IdeSelectDrive(base_port, drive_num, lba);
    if (result != IDE_OK) {
        SpinUnlockIrqRestore(&ide_lock, flags);
        return result;
    }
    
    // Set sector count and LBA
    outb(base_port + IDE_REG_SECTOR_COUNT, 1);
    outb(base_port + IDE_REG_LBA_LOW, lba & 0xFF);
    outb(base_port + IDE_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(base_port + IDE_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    
    // Send read command
    outb(base_port + IDE_REG_COMMAND, IDE_CMD_READ_SECTORS);
    
    // Wait for data ready
    result = IdeWaitData(base_port);
    if (result != IDE_OK) {
        SpinUnlockIrqRestore(&ide_lock, flags);
        return result;
    }
    
    // Read 256 words (512 bytes)
    uint16_t* buf16 = (uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        buf16[i] = inw(base_port + IDE_REG_DATA);
    }
    
    SpinUnlockIrqRestore(&ide_lock, flags);
    return IDE_OK;
}

int IdeWriteSector(uint8_t drive, uint32_t lba, const void* buffer) {
    if (drive >= 4) return IDE_ERROR_NO_DRIVE;
    
    uint8_t channel = drive / 2;
    uint8_t drive_num = drive % 2;
    
    if (!channels[channel].drive_exists[drive_num]) {
        return IDE_ERROR_NO_DRIVE;
    }
    
    irq_flags_t flags = SpinLockIrqSave(&ide_lock);
    
    uint16_t base_port = channels[channel].base_port;
    int result;
    
    // Select drive and set LBA
    result = IdeSelectDrive(base_port, drive_num, lba);
    if (result != IDE_OK) {
        SpinUnlockIrqRestore(&ide_lock, flags);
        return result;
    }
    
    // Set sector count and LBA
    outb(base_port + IDE_REG_SECTOR_COUNT, 1);
    outb(base_port + IDE_REG_LBA_LOW, lba & 0xFF);
    outb(base_port + IDE_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(base_port + IDE_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    
    // Send write command
    outb(base_port + IDE_REG_COMMAND, IDE_CMD_WRITE_SECTORS);
    
    // Wait for data ready
    result = IdeWaitData(base_port);
    if (result != IDE_OK) {
        SpinUnlockIrqRestore(&ide_lock, flags);
        return result;
    }
    
    // Write 256 words (512 bytes)
    const uint16_t* buf16 = (const uint16_t*)buffer;
    for (int i = 0; i < 256; i++) {
        outw(base_port + IDE_REG_DATA, buf16[i]);
    }
    
    // Wait for write completion
    result = IdeWaitReady(base_port);
    SpinUnlockIrqRestore(&ide_lock, flags);
    return result;
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