#include "RTL8139.h"
#include "../../../mm/KernelHeap.h"
#include "../../../mm/MemOps.h"
#include "../../../mm/VMem.h"
#include "../interface/Arp.h"
#include "../interface/Ip.h"
#include "Console.h"
#include "Io.h"
#include "ethernet/Packet.h"

// Global device object
static Rtl8139Device rtl_device;

void Rtl8139_Init() {
    PrintKernel("Searching for RTL8139 (10EC:8139)...\n");

    if (PciFindDevice(0x10EC, 0x8139, &rtl_device.pci_info) != 0) {
        PrintKernel("RTL8139 Network Card not found.\n");
        return;
    }
    PrintKernel("Found RTL8139!\n");

    // --- Part 1: Read BAR0 and get the I/O base address ---
    const uint32_t bar0 = PciConfigReadDWord(rtl_device.pci_info.bus, rtl_device.pci_info.device, rtl_device.pci_info.function, 0x10);
    if (bar0 & 0x1) {
        // I/O mapped
        rtl_device.io_base = bar0 & ~0x3;
    } else {
        PrintKernel("RTL8139 BAR0 is memory mapped, not supported.\n");
        return;
    }
    PrintKernel("I/O Base: 0x"); PrintKernelHex(rtl_device.io_base); PrintKernel("\n");

    // --- Part 2: Power on and Reset ---
    outb(rtl_device.io_base + REG_CONFIG_1, 0x00);
    outb(rtl_device.io_base + REG_COMMAND, CMD_RESET);
    while ((inb(rtl_device.io_base + REG_COMMAND) & CMD_RESET) != 0) { /* wait */ }
    PrintKernel("RTL8139 reset complete.\n");

    // --- Part 2.1: Reading MAC address ---
    PrintKernel("Reading MAC Address: ");
    for (int i = 0; i < 6; i++) {
        rtl_device.mac_address[i] = inb(rtl_device.io_base + REG_MAC0 + i);
        PrintKernelHex(rtl_device.mac_address[i]);
        if (i < 5) PrintKernel(":");
    }
    PrintKernel("\n");

    // --- Part 3: Allocate Buffers for DMA ---
    // NOTE: This MUST be physically contiguous memory! For now, we assume KernelMemoryAlloc does this.
    // In a real OS, you'd need a proper physical memory manager.
    rtl_device.rx_buffer = KernelMemoryAlloc(RX_BUFFER_SIZE);
    if (!rtl_device.rx_buffer) {
        PrintKernel("Failed to allocate RX buffer.\n");
        return;
    }

    for (int i = 0; i < TX_BUFFER_COUNT; i++) {
        rtl_device.tx_buffers[i] = KernelMemoryAlloc(2048); // 2K per TX buffer
        if (!rtl_device.tx_buffers[i]) {
            PrintKernel("Failed to allocate TX buffer.\n");
            // Clean up previously allocated buffers
            for (int j = 0; j < i; j++) {
                KernelFree(rtl_device.tx_buffers[j]);
            }
            KernelFree(rtl_device.rx_buffer);
            return;
        }
    }

    rtl_device.current_tx_buffer = 0;
    PrintKernel("DMA buffers allocated.\n");

    // --- Part 4: Tell the card where the receive buffer is ---
    // The hardware needs the PHYSICAL address of the buffer.
    uint32_t rx_phys_addr = VMemGetPhysAddr((uint64_t)rtl_device.rx_buffer);
    outl(rtl_device.io_base + REG_RX_BUFFER_START, rx_phys_addr);
    PrintKernel("Receive buffer configured.\n");

    // --- Part 5: Enable the Receiver and Transmitter ---
    // This is the final step to "turn on" the card.
    outb(rtl_device.io_base + REG_COMMAND, CMD_TX_ENABLE | CMD_RX_ENABLE);
    PrintKernel("Transmitter and Receiver enabled.\n");

    // Configure Rx register: accept broadcast, multicast, and packets for our MAC (AB+AM+APM)
    // and wrap packets that are too long.
    outl(rtl_device.io_base + REG_RX_CONFIG, (1 << 7) | (1 << 3) | (1 << 2) | (1 << 1));

    PrintKernel("RTL8139 initialization finished!\n");
}

void Rtl8139_SendPacket(void* data, uint32_t len) {
    if (len > 2048) {
        PrintKernel("Packet too large to send.\n");
        return;
    }

    // Get the I/O and memory addresses for the current transmit descriptor
    int tx_index = rtl_device.current_tx_buffer;
    uint32_t tx_addr_reg = REG_TX_ADDR_0 + (tx_index * 4);
    uint32_t tx_stat_reg = REG_TX_STATUS_0 + (tx_index * 4);
    uint8_t* tx_buffer = rtl_device.tx_buffers[tx_index];

    // Copy the packet data to our DMA-safe buffer
    FastMemcpy(tx_buffer, data, len);

    // Tell the card where the data is (physical address)
    uint32_t tx_phys_addr = (uint32_t)tx_buffer;
    outl(rtl_device.io_base + tx_addr_reg, tx_phys_addr);

    // Tell the card the length of the data and start sending!
    outl(rtl_device.io_base + tx_stat_reg, len);

    PrintKernel("Sent packet of "); PrintKernelInt(len); PrintKernel(" bytes.\n");

    // Move to the next transmit buffer for the next send operation
    rtl_device.current_tx_buffer = (tx_index + 1) % TX_BUFFER_COUNT;
}

const Rtl8139Device* GetRtl8139Device() {
    return &rtl_device;
}

void Rtl8139_HandleReceive() {
    uint16_t status = inw(rtl_device.io_base + REG_ISR);

    if (status & ISR_RX_OK) {
        uint8_t* received_data = rtl_device.rx_buffer + rtl_device.current_rx_offset;
        uint16_t packet_len = *(uint16_t*)(received_data + 2);

        EthernetHeader* eth_header = (EthernetHeader*)received_data;
        if (eth_header->ethertype == HTONS(0x0800)) { // IPv4
            IpHandlePacket((IpHeader*)(received_data + sizeof(EthernetHeader)), packet_len - sizeof(EthernetHeader));
        } else if (eth_header->ethertype == HTONS(0x0806)) { // ARP
            ArpHandlePacket(eth_header, packet_len);
        }

        rtl_device.current_rx_offset = (rtl_device.current_rx_offset + packet_len + 4 + 3) & ~3;
        if (rtl_device.current_rx_offset > RX_BUFFER_SIZE) {
            rtl_device.current_rx_offset -= RX_BUFFER_SIZE;
        }

        outw(rtl_device.io_base + REG_CAPR, rtl_device.current_rx_offset - 0x10);
        outw(rtl_device.io_base + REG_ISR, ISR_RX_OK);
    }
}

