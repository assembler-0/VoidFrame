#include <E1000.h>
#include <../interface/Arp.h>
#include <../interface/Ip.h>
#include <Console.h>
#include <TSC.h>
#include <Io.h>
#include <KernelHeap.h>
#include <MemOps.h>
#include <PCI/PCI.h>
#include <PMem.h>
#include <ethernet/Packet.h>

static E1000Device g_e1000_device = {0};

static uint32_t E1000_ReadReg(uint32_t reg) {
    return *(volatile uint32_t*)(g_e1000_device.mmio_base + reg);
}

static void E1000_WriteReg(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(g_e1000_device.mmio_base + reg) = value;
}

static uint16_t E1000_ReadEEPROM(uint8_t addr) {
    E1000_WriteReg(E1000_EERD, (addr << 8) | 1);
    uint32_t val;
    do {
        val = E1000_ReadReg(E1000_EERD);
    } while (!(val & (1 << 4)));
    return (val >> 16) & 0xFFFF;
}

static void E1000_ReadMAC(void) {
    uint16_t mac01 = E1000_ReadEEPROM(0);
    uint16_t mac23 = E1000_ReadEEPROM(1);
    uint16_t mac45 = E1000_ReadEEPROM(2);
    
    g_e1000_device.mac_address[0] = mac01 & 0xFF;
    g_e1000_device.mac_address[1] = (mac01 >> 8) & 0xFF;
    g_e1000_device.mac_address[2] = mac23 & 0xFF;
    g_e1000_device.mac_address[3] = (mac23 >> 8) & 0xFF;
    g_e1000_device.mac_address[4] = mac45 & 0xFF;
    g_e1000_device.mac_address[5] = (mac45 >> 8) & 0xFF;
}

static void E1000_InitRX(void) {
    // Allocate RX descriptors
    g_e1000_device.rx_descs = (E1000RxDesc*)KernelMemoryAlloc(sizeof(E1000RxDesc) * E1000_NUM_RX_DESC);
    g_e1000_device.rx_buffers = (uint8_t**)KernelMemoryAlloc(sizeof(uint8_t*) * E1000_NUM_RX_DESC);
    
    // Allocate RX buffers
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        g_e1000_device.rx_buffers[i] = (uint8_t*)KernelMemoryAlloc(2048);
        g_e1000_device.rx_descs[i].addr = (uint64_t)g_e1000_device.rx_buffers[i];
        g_e1000_device.rx_descs[i].status = 0;
    }
    
    // Set RX descriptor base and length
    uint64_t rx_desc_phys = (uint64_t)g_e1000_device.rx_descs;
    E1000_WriteReg(E1000_RDBAL, rx_desc_phys & 0xFFFFFFFF);
    E1000_WriteReg(E1000_RDBAH, (rx_desc_phys >> 32) & 0xFFFFFFFF);
    E1000_WriteReg(E1000_RDLEN, E1000_NUM_RX_DESC * sizeof(E1000RxDesc));
    
    // Set RX head and tail
    E1000_WriteReg(E1000_RDH, 0);
    E1000_WriteReg(E1000_RDT, E1000_NUM_RX_DESC - 1);
    
    g_e1000_device.rx_cur = 0;
    
    // Enable RX
    E1000_WriteReg(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE_2048);
}

static void E1000_InitTX(void) {
    // Allocate TX descriptors
    g_e1000_device.tx_descs = (E1000TxDesc*)KernelMemoryAlloc(sizeof(E1000TxDesc) * E1000_NUM_TX_DESC);
    g_e1000_device.tx_buffers = (uint8_t**)KernelMemoryAlloc(sizeof(uint8_t*) * E1000_NUM_TX_DESC);
    
    // Allocate TX buffers
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        g_e1000_device.tx_buffers[i] = (uint8_t*)KernelMemoryAlloc(2048);
        g_e1000_device.tx_descs[i].addr = (uint64_t)g_e1000_device.tx_buffers[i];
        g_e1000_device.tx_descs[i].status = 1; // DD bit set
    }
    
    // Set TX descriptor base and length
    uint64_t tx_desc_phys = (uint64_t)g_e1000_device.tx_descs;
    E1000_WriteReg(E1000_TDBAL, tx_desc_phys & 0xFFFFFFFF);
    E1000_WriteReg(E1000_TDBAH, (tx_desc_phys >> 32) & 0xFFFFFFFF);
    E1000_WriteReg(E1000_TDLEN, E1000_NUM_TX_DESC * sizeof(E1000TxDesc));
    
    // Set TX head and tail
    E1000_WriteReg(E1000_TDH, 0);
    E1000_WriteReg(E1000_TDT, 0);
    
    g_e1000_device.tx_cur = 0;
    
    // Enable TX
    E1000_WriteReg(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);
}

int E1000_Init(void) {
    
    // Find E1000 device
    PciDevice pci_dev;
    if (PciFindDevice(E1000_VENDOR_ID, E1000_DEVICE_ID, &pci_dev) != 0) {
        PrintKernel("E1000: Device not found\n");
        return -1;
    }
    
    PrintKernel("E1000: Found device at ");
    PrintKernelInt(pci_dev.bus);
    PrintKernel(":");
    PrintKernelInt(pci_dev.device);
    PrintKernel(":");
    PrintKernelInt(pci_dev.function);
    PrintKernel("\n");
    
    // Get MMIO base address
    g_e1000_device.mmio_base = pci_dev.bar0 & ~0xF;
    
    // Enable bus mastering
    uint16_t cmd = PciReadConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    PciWriteConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd | 0x04);
    
    // Reset device
    E1000_WriteReg(E1000_CTRL, E1000_CTRL_RST);
    delay(10000);
    
    // Read MAC address
    E1000_ReadMAC();
    PrintKernel("E1000: MAC Address: ");
    for (int i = 0; i < 6; i++) {
        PrintKernelHex(g_e1000_device.mac_address[i]);
        if (i < 5) PrintKernel(":");
    }
    PrintKernel("\n");
    
    // Set MAC address in RAL/RAH
    uint32_t ral = g_e1000_device.mac_address[0] |
                   (g_e1000_device.mac_address[1] << 8) |
                   (g_e1000_device.mac_address[2] << 16) |
                   (g_e1000_device.mac_address[3] << 24);
    uint32_t rah = g_e1000_device.mac_address[4] |
                   (g_e1000_device.mac_address[5] << 8) |
                   (1 << 31); // Address Valid bit
    
    E1000_WriteReg(E1000_RAL, ral);
    E1000_WriteReg(E1000_RAH, rah);
    
    // Initialize RX and TX
    E1000_InitRX();
    E1000_InitTX();
    
    // Link up
    E1000_WriteReg(E1000_CTRL, E1000_CTRL_SLU | E1000_CTRL_ASDE);
    
    g_e1000_device.initialized = 1;
    PrintKernelSuccess("E1000: Driver initialized successfully\n");
    
    return 0;
}

int E1000_SendPacket(const void* data, uint16_t length) {
    if (!g_e1000_device.initialized || length > 1518) {
        return -1;
    }
    
    // Wait for descriptor to be available
    while (!(g_e1000_device.tx_descs[g_e1000_device.tx_cur].status & 1)) {
        // Descriptor not ready
    }
    
    // Copy data to buffer
    FastMemcpy(g_e1000_device.tx_buffers[g_e1000_device.tx_cur], data, length);
    
    // Set up descriptor
    g_e1000_device.tx_descs[g_e1000_device.tx_cur].length = length;
    g_e1000_device.tx_descs[g_e1000_device.tx_cur].cmd = (1 << 3) | (1 << 1) | 1; // RS | IFCS | EOP
    g_e1000_device.tx_descs[g_e1000_device.tx_cur].status = 0;
    
    // Update tail pointer
    uint16_t old_cur = g_e1000_device.tx_cur;
    g_e1000_device.tx_cur = (g_e1000_device.tx_cur + 1) % E1000_NUM_TX_DESC;
    E1000_WriteReg(E1000_TDT, g_e1000_device.tx_cur);
    
    return 0;
}

const E1000Device* E1000_GetDevice(void) {
    return g_e1000_device.initialized ? &g_e1000_device : NULL;
}

void E1000_HandleReceive(void) {
    if (!g_e1000_device.initialized) {
        return;
    }

    uint16_t old_cur;
    while (g_e1000_device.rx_descs[g_e1000_device.rx_cur].status & 1) {
        uint8_t* received_data = g_e1000_device.rx_buffers[g_e1000_device.rx_cur];
        uint16_t received_length = g_e1000_device.rx_descs[g_e1000_device.rx_cur].length;

        EthernetHeader* eth_header = (EthernetHeader*)received_data;

        if (eth_header->ethertype == HTONS(0x0800)) { // IPv4
            IpHandlePacket((IpHeader*)(received_data + sizeof(EthernetHeader)), received_length - sizeof(EthernetHeader));
        } else if (eth_header->ethertype == HTONS(0x0806)) { // ARP
            ArpHandlePacket(eth_header, received_length);
        }

        // Reset descriptor
        g_e1000_device.rx_descs[g_e1000_device.rx_cur].status = 0;
        old_cur = g_e1000_device.rx_cur;
        g_e1000_device.rx_cur = (g_e1000_device.rx_cur + 1) % E1000_NUM_RX_DESC;
        E1000_WriteReg(E1000_RDT, old_cur);
    }
}