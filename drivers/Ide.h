#pragma once

#include <stdint.h>

// IDE Controller Ports
#define IDE_PRIMARY_BASE    0x1F0
#define IDE_SECONDARY_BASE  0x170
#define IDE_PRIMARY_CTRL    0x3F6
#define IDE_SECONDARY_CTRL  0x376

// IDE Registers (offset from base)
#define IDE_REG_DATA        0x00
#define IDE_REG_ERROR       0x01
#define IDE_REG_FEATURES    0x01
#define IDE_REG_SECTOR_COUNT 0x02
#define IDE_REG_LBA_LOW     0x03
#define IDE_REG_LBA_MID     0x04
#define IDE_REG_LBA_HIGH    0x05
#define IDE_REG_DRIVE_HEAD  0x06
#define IDE_REG_STATUS      0x07
#define IDE_REG_COMMAND     0x07

// IDE Status Bits
#define IDE_STATUS_ERR      0x01
#define IDE_STATUS_DRQ      0x08
#define IDE_STATUS_SRV      0x10
#define IDE_STATUS_DF       0x20
#define IDE_STATUS_RDY      0x40
#define IDE_STATUS_BSY      0x80

// IDE Commands
#define IDE_CMD_READ_SECTORS    0x20
#define IDE_CMD_WRITE_SECTORS   0x30
#define IDE_CMD_IDENTIFY        0xEC

// Drive Selection
#define IDE_DRIVE_MASTER    0
#define IDE_DRIVE_SLAVE     1

// Error Codes
#define IDE_OK              0
#define IDE_ERROR_TIMEOUT   -1
#define IDE_ERROR_NOT_READY -2
#define IDE_ERROR_NO_DRIVE  -3
#define IDE_ERROR_IO        -4

typedef struct {
    uint16_t base_port;
    uint16_t ctrl_port;
    uint8_t drive_exists[2];  // master/slave
    char model[2][41];        // drive model strings
} IdeChannel;

// Core Functions
int IdeInit(void);
int IdeReadSector(uint8_t drive, uint32_t lba, void* buffer);
int IdeWriteSector(uint8_t drive, uint32_t lba, const void* buffer);
int IdeGetDriveInfo(uint8_t drive, char* model_out);

// Interrupt handlers
void IDEPrimaryIRQH(void);
void IDESecondaryIRQH(void);

