#ifndef VOIDFRAME_AHCI_H
#define VOIDFRAME_AHCI_H

#include <stdint.h>
#include <PCI/PCI.h>

// AHCI PCI Class/Subclass
#define AHCI_CLASS_CODE     0x01
#define AHCI_SUBCLASS       0x06
#define AHCI_PROG_IF        0x01

// AHCI Register Offsets
#define AHCI_CAP            0x00    // Host Capabilities
#define AHCI_GHC            0x04    // Global Host Control
#define AHCI_IS             0x08    // Interrupt Status
#define AHCI_PI             0x0C    // Ports Implemented
#define AHCI_VS             0x10    // Version
#define AHCI_BOHC           0x28    // BIOS/OS Handoff Control

// Port Registers (offset 0x100 + port * 0x80)
#define AHCI_PORT_CLB       0x00    // Command List Base Address
#define AHCI_PORT_CLBU      0x04    // Command List Base Address Upper
#define AHCI_PORT_FB        0x08    // FIS Base Address
#define AHCI_PORT_FBU       0x0C    // FIS Base Address Upper
#define AHCI_PORT_IS        0x10    // Interrupt Status
#define AHCI_PORT_IE        0x14    // Interrupt Enable
#define AHCI_PORT_CMD       0x18    // Command and Status
#define AHCI_PORT_TFD       0x20    // Task File Data
#define AHCI_PORT_SIG       0x24    // Signature
#define AHCI_PORT_SSTS      0x28    // SATA Status
#define AHCI_PORT_SCTL      0x2C    // SATA Control
#define AHCI_PORT_SERR      0x30    // SATA Error
#define AHCI_PORT_SACT      0x34    // SATA Active
#define AHCI_PORT_CI        0x38    // Command Issue

// Global Host Control bits
#define AHCI_GHC_AE         (1 << 31)  // AHCI Enable
#define AHCI_GHC_IE         (1 << 1)   // Interrupt Enable
#define AHCI_GHC_HR         (1 << 0)   // HBA Reset

// Port Command bits
#define AHCI_PORT_CMD_ST    (1 << 0)   // Start
#define AHCI_PORT_CMD_FRE   (1 << 4)   // FIS Receive Enable
#define AHCI_PORT_CMD_FR    (1 << 14)  // FIS Receive Running
#define AHCI_PORT_CMD_CR    (1 << 15)  // Command List Running

// SATA Status bits
#define AHCI_PORT_SSTS_DET_MASK  0x0F
#define AHCI_PORT_SSTS_DET_PRESENT  0x03

// Command Header
typedef struct {
    uint8_t cfl:5;      // Command FIS Length
    uint8_t a:1;        // ATAPI
    uint8_t w:1;        // Write
    uint8_t p:1;        // Prefetchable
    uint8_t r:1;        // Reset
    uint8_t b:1;        // BIST
    uint8_t c:1;        // Clear Busy upon R_OK
    uint8_t rsvd0:1;
    uint8_t pmp:4;      // Port Multiplier Port
    uint16_t prdtl;     // Physical Region Descriptor Table Length
    uint32_t prdbc;     // Physical Region Descriptor Byte Count
    uint64_t ctba;      // Command Table Base Address
    uint32_t rsvd1[4];
} __attribute__((packed)) AHCICmdHeader;

// Physical Region Descriptor
typedef struct {
    uint64_t dba;       // Data Base Address
    uint32_t rsvd0;
    uint32_t dbc:22;    // Data Byte Count
    uint32_t rsvd1:9;
    uint32_t i:1;       // Interrupt on completion
} __attribute__((packed)) AHCIPrd;

// Command Table
typedef struct {
    uint8_t cfis[64];   // Command FIS
    uint8_t acmd[16];   // ATAPI Command
    uint8_t rsvd[48];
    AHCIPrd prdt[1];    // Physical Region Descriptor Table
} __attribute__((packed)) AHCICmdTable;

// Register FIS - Host to Device
typedef struct {
    uint8_t fis_type;   // FIS_TYPE_REG_H2D
    uint8_t pmport:4;   // Port multiplier
    uint8_t rsvd0:3;
    uint8_t c:1;        // 1: Command, 0: Control
    uint8_t command;    // Command register
    uint8_t featurel;   // Feature register, 7:0
    uint8_t lba0;       // LBA low register, 7:0
    uint8_t lba1;       // LBA mid register, 15:8
    uint8_t lba2;       // LBA high register, 23:16
    uint8_t device;     // Device register
    uint8_t lba3;       // LBA register, 31:24
    uint8_t lba4;       // LBA register, 39:32
    uint8_t lba5;       // LBA register, 47:40
    uint8_t featureh;   // Feature register, 15:8
    uint8_t countl;     // Count register, 7:0
    uint8_t counth;     // Count register, 15:8
    uint8_t icc;        // Isochronous command completion
    uint8_t control;    // Control register
    uint8_t rsvd1[4];
} __attribute__((packed)) FISRegH2D;

// AHCI Port structure
typedef struct {
    volatile uint32_t* regs;
    AHCICmdHeader* cmd_list;
    uint8_t* fis_base;
    AHCICmdTable* cmd_table;
    uint64_t cmd_list_phys;
    uint64_t fis_base_phys;
    uint64_t cmd_table_phys;
    int port_num;
    int active;
} AHCIPort;

// AHCI Controller structure
typedef struct {
    PciDevice pci_device;
    volatile uint8_t* mmio_base;
    uint64_t mmio_size;
    uint32_t ports_implemented;
    AHCIPort ports[32];
    int initialized;
} AHCIController;

// Function prototypes
int AHCI_Init(void);
int AHCI_ReadSectors(int port, uint64_t lba, uint16_t count, void* buffer);
int AHCI_WriteSectors(int port, uint64_t lba, uint16_t count, const void* buffer);
const AHCIController* AHCI_GetController(void);

#endif // VOIDFRAME_AHCI_H