#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include "PCI/PCI.h"

// =============================================================================
//         RTL8139 Register Offsets (from the I/O base address)
// =============================================================================
#define REG_MAC0              0x00  // MAC Address (6 bytes)
#define REG_MAR0              0x08  // Multicast Address Register (8 bytes)
#define REG_TX_STATUS_0       0x10  // Transmit Status of Descriptor 0 (4 bytes)
#define REG_TX_ADDR_0         0x20  // Transmit Address of Descriptor 0 (4 bytes)
#define REG_RX_BUFFER_START   0x30  // Receive Buffer Start Address (RBSTART)
#define REG_COMMAND           0x37  // Command Register (1 byte)
#define REG_CAPR              0x38  // Current Address of Packet Read (2 bytes)
#define REG_IMR               0x3C  // Interrupt Mask Register (2 bytes)
#define REG_ISR               0x3E  // Interrupt Service Register (2 bytes)
#define REG_TX_CONFIG         0x40  // Tx Config Register (4 bytes)
#define REG_RX_CONFIG         0x44  // Rx Config Register (4 bytes)
#define REG_CONFIG_1          0x52  // Config Register 1 (1 byte)

// =============================================================================
//                    Command Register (REG_COMMAND) Bits
// =============================================================================
#define CMD_BUFFER_EMPTY      (1 << 0)  // Is receive buffer empty?
#define CMD_TX_ENABLE         (1 << 2)  // Enable Transmitter
#define CMD_RX_ENABLE         (1 << 3)  // Enable Receiver
#define CMD_RESET             (1 << 4)  // Software Reset

// =============================================================================
//                 Interrupt Service/Mask Register (ISR/IMR) Bits
// =============================================================================
#define ISR_RX_OK             (1 << 0)  // Receive OK
#define ISR_TX_OK             (1 << 2)  // Transmit OK
#define ISR_RX_ERR            (1 << 1)  // Receive Error
#define ISR_TX_ERR            (1 << 3)  // Transmit Error

// =============================================================================
//                    Driver Configuration Constants
// =============================================================================
#define RX_BUFFER_SIZE        (8192 + 16) // 8K + 16-byte header
#define TX_BUFFER_COUNT       4           // Use all 4 hardware transmit buffers

// Device state structure
typedef struct {
    PciDevice pci_info;
    uint32_t io_base;
    uint8_t mac_address[6];
    uint8_t* rx_buffer;
    uint8_t* tx_buffers[TX_BUFFER_COUNT];
    int current_tx_buffer;
} Rtl8139Device;

// Function Prototypes
void Rtl8139_Init();
void Rtl8139_SendPacket(void* data, uint32_t len);
const Rtl8139Device* GetRtl8139Device();

#endif // RTL8139_H