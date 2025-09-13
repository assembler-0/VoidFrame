#ifndef VOIDFRAME_E1000_H
#define VOIDFRAME_E1000_H

#include "stdint.h"

// E1000 PCI IDs
#define E1000_VENDOR_ID     0x8086
#define E1000_DEVICE_ID     0x100E  // 82540EM

// Register offsets
#define E1000_CTRL          0x0000  // Device Control
#define E1000_STATUS        0x0008  // Device Status
#define E1000_EECD          0x0010  // EEPROM Control
#define E1000_EERD          0x0014  // EEPROM Read
#define E1000_ICR           0x00C0  // Interrupt Cause Read
#define E1000_IMS           0x00D0  // Interrupt Mask Set
#define E1000_IMC           0x00D8  // Interrupt Mask Clear
#define E1000_RCTL          0x0100  // RX Control
#define E1000_RDBAL         0x2800  // RX Descriptor Base Address Low
#define E1000_RDBAH         0x2804  // RX Descriptor Base Address High
#define E1000_RDLEN         0x2808  // RX Descriptor Length
#define E1000_RDH           0x2810  // RX Descriptor Head
#define E1000_RDT           0x2818  // RX Descriptor Tail
#define E1000_TCTL          0x0400  // TX Control
#define E1000_TDBAL         0x3800  // TX Descriptor Base Address Low
#define E1000_TDBAH         0x3804  // TX Descriptor Base Address High
#define E1000_TDLEN         0x3808  // TX Descriptor Length
#define E1000_TDH           0x3810  // TX Descriptor Head
#define E1000_TDT           0x3818  // TX Descriptor Tail
#define E1000_RAL           0x5400  // Receive Address Low
#define E1000_RAH           0x5404  // Receive Address High

// Control Register bits
#define E1000_CTRL_RST      (1 << 26)  // Device Reset
#define E1000_CTRL_ASDE     (1 << 5)   // Auto-Speed Detection Enable
#define E1000_CTRL_SLU      (1 << 6)   // Set Link Up

// RX Control bits
#define E1000_RCTL_EN       (1 << 1)   // Receiver Enable
#define E1000_RCTL_BAM      (1 << 15)  // Broadcast Accept Mode
#define E1000_RCTL_BSIZE_2048 (0 << 16) // Buffer Size 2048

// TX Control bits
#define E1000_TCTL_EN       (1 << 1)   // Transmitter Enable
#define E1000_TCTL_PSP      (1 << 3)   // Pad Short Packets

// Descriptor counts
#define E1000_NUM_RX_DESC   32
#define E1000_NUM_TX_DESC   32

// RX Descriptor
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) E1000RxDesc;

// TX Descriptor
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) E1000TxDesc;

// Device structure
typedef struct {
    uint32_t mmio_base;
    uint8_t mac_address[6];
    E1000RxDesc* rx_descs;
    E1000TxDesc* tx_descs;
    uint8_t** rx_buffers;
    uint8_t** tx_buffers;
    uint16_t rx_cur;
    uint16_t tx_cur;
    int initialized;
} E1000Device;

// Function prototypes
int E1000_Init(void);
int E1000_SendPacket(const void* data, uint16_t length);
const E1000Device* E1000_GetDevice(void);
void E1000_HandleReceive(void);

#endif // VOIDFRAME_E1000_H