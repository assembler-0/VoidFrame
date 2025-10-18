#include "AHCI.h"
#include "Console.h"
#include "TSC.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "VMem.h"
#include "BlockDevice.h"
#include "DriveNaming.h"
#include "Format.h"
#include "PCI/PCI.h"

#define FIS_TYPE_REG_H2D    0x27
#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY    0xEC

static AHCIController g_ahci_controller = {0};

// Forward declarations
static int AHCI_ReadBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer);
static int AHCI_WriteBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer);

static uint32_t AHCI_ReadReg(uint32_t offset) {
    return *(volatile uint32_t*)(g_ahci_controller.mmio_base + offset);
}

static void AHCI_WriteReg(uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(g_ahci_controller.mmio_base + offset) = value;
}

static uint32_t AHCI_ReadPortReg(int port, uint32_t offset) {
    uint32_t port_base = 0x100 + (port * 0x80);
    return *(volatile uint32_t*)(g_ahci_controller.mmio_base + port_base + offset);
}

static void AHCI_WritePortReg(int port, uint32_t offset, uint32_t value) {
    uint32_t port_base = 0x100 + (port * 0x80);
    *(volatile uint32_t*)(g_ahci_controller.mmio_base + port_base + offset) = value;
}

static int AHCI_StopPort(int port) {
    // Clear ST bit
    uint32_t cmd = AHCI_ReadPortReg(port, AHCI_PORT_CMD);
    cmd &= ~AHCI_PORT_CMD_ST;
    AHCI_WritePortReg(port, AHCI_PORT_CMD, cmd);
    
    // Wait for CR to clear
    int timeout = 5000;
    while (timeout-- > 0) {
        cmd = AHCI_ReadPortReg(port, AHCI_PORT_CMD);
        if (!(cmd & AHCI_PORT_CMD_CR)) break;
        delay_us(500);
    }
    
    // Clear FRE bit
    cmd = AHCI_ReadPortReg(port, AHCI_PORT_CMD);
    cmd &= ~AHCI_PORT_CMD_FRE;
    AHCI_WritePortReg(port, AHCI_PORT_CMD, cmd);
    
    // Wait for FR to clear
    timeout = 5000;
    while (timeout-- > 0) {
        cmd = AHCI_ReadPortReg(port, AHCI_PORT_CMD);
        if (!(cmd & AHCI_PORT_CMD_FR)) break;
        delay_us(500);
    }
    
    return (timeout > 0) ? 0 : -1;
}

static int AHCI_StartPort(int port) {
    // Set FRE bit
    uint32_t cmd = AHCI_ReadPortReg(port, AHCI_PORT_CMD);
    cmd |= AHCI_PORT_CMD_FRE;
    AHCI_WritePortReg(port, AHCI_PORT_CMD, cmd);
    
    // Set ST bit
    cmd |= AHCI_PORT_CMD_ST;
    AHCI_WritePortReg(port, AHCI_PORT_CMD, cmd);
    
    return 0;
}

static int AHCI_InitPort(int port) {
    PrintKernel("AHCI: Initializing port ");
    PrintKernelInt(port);
    PrintKernel("...\n");
    
    AHCIPort* ahci_port = &g_ahci_controller.ports[port];
    ahci_port->port_num = port;
    
    // Stop port
    if (AHCI_StopPort(port) != 0) {
        PrintKernel("AHCI: Failed to stop port\n");
        return -1;
    }
    
    // Allocate command list (1KB, 32 entries)
    ahci_port->cmd_list = (AHCICmdHeader*)VMemAlloc(1024);
    if (!ahci_port->cmd_list) {
        PrintKernel("AHCI: Failed to allocate command list\n");
        return -1;
    }
    FastMemset(ahci_port->cmd_list, 0, 1024);
    
    // Allocate FIS receive area (256 bytes)
    ahci_port->fis_base = (uint8_t*)VMemAlloc(256);
    if (!ahci_port->fis_base) {
        PrintKernel("AHCI: Failed to allocate FIS base\n");
        VMemFree(ahci_port->cmd_list, 1024);
        return -1;
    }
    FastMemset(ahci_port->fis_base, 0, 256);
    
    // Allocate command table (256 bytes)
    ahci_port->cmd_table = (AHCICmdTable*)VMemAlloc(256);
    if (!ahci_port->cmd_table) {
        PrintKernel("AHCI: Failed to allocate command table\n");
        VMemFree(ahci_port->cmd_list, 1024);
        VMemFree(ahci_port->fis_base, 256);
        return -1;
    }
    FastMemset(ahci_port->cmd_table, 0, 256);
    
    // Get physical addresses
    ahci_port->cmd_list_phys = VMemGetPhysAddr((uint64_t)ahci_port->cmd_list);
    ahci_port->fis_base_phys = VMemGetPhysAddr((uint64_t)ahci_port->fis_base);
    ahci_port->cmd_table_phys = VMemGetPhysAddr((uint64_t)ahci_port->cmd_table);
    
    // Set up command list base address
    AHCI_WritePortReg(port, AHCI_PORT_CLB, ahci_port->cmd_list_phys & 0xFFFFFFFF);
    AHCI_WritePortReg(port, AHCI_PORT_CLBU, (ahci_port->cmd_list_phys >> 32) & 0xFFFFFFFF);
    
    // Set up FIS base address
    AHCI_WritePortReg(port, AHCI_PORT_FB, ahci_port->fis_base_phys & 0xFFFFFFFF);
    AHCI_WritePortReg(port, AHCI_PORT_FBU, (ahci_port->fis_base_phys >> 32) & 0xFFFFFFFF);
    
    // Set up command header
    AHCICmdHeader* cmd_hdr = &ahci_port->cmd_list[0];
    cmd_hdr->cfl = sizeof(FISRegH2D) / 4;
    cmd_hdr->prdtl = 1;
    cmd_hdr->ctba = ahci_port->cmd_table_phys;
    
    // Clear interrupt status
    AHCI_WritePortReg(port, AHCI_PORT_IS, 0xFFFFFFFF);
    
    // Start port
    AHCI_StartPort(port);
    
    ahci_port->active = 1;
    PrintKernel("AHCI: Port initialized\n");
    return 0;
}

static int AHCI_SendCommand(int port, uint8_t command, uint64_t lba, uint16_t count, void* buffer, int write) {
    AHCIPort* ahci_port = &g_ahci_controller.ports[port];
    if (!ahci_port->active) return -1;
    
    // Wait for port to be ready
    int timeout = 2000;
    while (timeout-- > 0) {
        uint32_t tfd = AHCI_ReadPortReg(port, AHCI_PORT_TFD);
        if (!(tfd & 0x88)) break; // BSY and DRQ clear
        delay_us(500);
    }
    if (timeout <= 0) return -1;
    
    // Set up command FIS
    FISRegH2D* fis = (FISRegH2D*)ahci_port->cmd_table->cfis;
    FastMemset(fis, 0, sizeof(FISRegH2D));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1; // Command
    fis->command = command;
    fis->device = 1 << 6; // LBA mode
    
    fis->lba0 = lba & 0xFF;
    fis->lba1 = (lba >> 8) & 0xFF;
    fis->lba2 = (lba >> 16) & 0xFF;
    fis->lba3 = (lba >> 24) & 0xFF;
    fis->lba4 = (lba >> 32) & 0xFF;
    fis->lba5 = (lba >> 40) & 0xFF;
    
    fis->countl = count & 0xFF;
    fis->counth = (count >> 8) & 0xFF;
    
    // Set up PRD
    AHCIPrd* prd = &ahci_port->cmd_table->prdt[0];
    prd->dba = VMemGetPhysAddr((uint64_t)buffer);
    prd->dbc = (count * 512) - 1;
    prd->i = 1; // Interrupt on completion
    
    // Set up command header
    AHCICmdHeader* cmd_hdr = &ahci_port->cmd_list[0];
    cmd_hdr->w = write ? 1 : 0;
    
    // Issue command
    AHCI_WritePortReg(port, AHCI_PORT_CI, 1);
    
    // Wait for completion
    timeout = 5000;
    while (timeout-- > 0) {
        uint32_t ci = AHCI_ReadPortReg(port, AHCI_PORT_CI);
        if (!(ci & 1)) break;
        delay_us(50);
    }
    
    if (timeout <= 0) {
        PrintKernel("AHCI: Command timeout\n");
        return -1;
    }

    // Check for errors
    uint32_t is = AHCI_ReadPortReg(port, AHCI_PORT_IS);
    if (is & 0x40000000) { // Task file error
        PrintKernel("AHCI: Task file error\n");
        return -1;
    }
    
    // Clear interrupt status
    AHCI_WritePortReg(port, AHCI_PORT_IS, 0xFFFFFFFF);
    
    return 0;
}

static uint64_t AHCI_GetDriveCapacity(int port) {
    // Allocate buffer for IDENTIFY data
    uint16_t* identify_data = (uint16_t*)KernelMemoryAlloc(512);
    if (!identify_data) return 0;
    
    // Send IDENTIFY command
    int result = AHCI_SendCommand(port, ATA_CMD_IDENTIFY, 0, 1, identify_data, 0);
    if (result != 0) {
        KernelFree(identify_data);
        return 0x1000000; // Fallback size (8GB)
    }
    
    // Get total sectors from IDENTIFY data
    // For LBA48: words 100-103 contain total sectors
    // For LBA28: words 60-61 contain total sectors
    uint64_t total_sectors = 0;
    
    // Check if LBA48 is supported (bit 10 of word 83)
    if (identify_data[83] & (1 << 10)) {
        // LBA48 - use words 100-103
        total_sectors = *(uint64_t*)(identify_data + 100);
    } else {
        // LBA28 - use words 60-61
        total_sectors = *(uint32_t*)(identify_data + 60);
    }
    
    KernelFree(identify_data);
    
    // Sanity check
    if (total_sectors == 0 || total_sectors > 0x1000000000ULL) {
        return 0x1000000; // Fallback to 8GB
    }
    
    return total_sectors;
}

int AHCI_ReadSectors(int port, uint64_t lba, uint16_t count, void* buffer) {
    if (!g_ahci_controller.initialized || !g_ahci_controller.ports[port].active) {
        return -1;
    }
    
    // Use DMA-based READ for AHCI
    return AHCI_SendCommand(port, ATA_CMD_READ_DMA_EX, lba, count, buffer, 0);
}

int AHCI_WriteSectors(int port, uint64_t lba, uint16_t count, const void* buffer) {
    if (!g_ahci_controller.initialized || !g_ahci_controller.ports[port].active) {
        return -1;
    }
    
    // Use DMA-based WRITE for AHCI
    return AHCI_SendCommand(port, ATA_CMD_WRITE_DMA_EX, lba, count, (void*)buffer, 1);
}

int AHCI_Init(void) {
    PrintKernel("AHCI: Initializing AHCI driver...\n");
    
    // Find AHCI controller
    PciDevice pci_dev;
    if (PciFindByClass(AHCI_CLASS_CODE, AHCI_SUBCLASS, AHCI_PROG_IF, &pci_dev) != 0) {
        PrintKernel("AHCI: No AHCI controller found\n");
        return -1;
    }
    
    PrintKernel("AHCI: Found controller at ");
    PrintKernelInt(pci_dev.bus);
    PrintKernel(":");
    PrintKernelInt(pci_dev.device);
    PrintKernel(":");
    PrintKernelInt(pci_dev.function);
    PrintKernel("\n");
    
    g_ahci_controller.pci_device = pci_dev;
    
    // Enable bus mastering and memory space
    uint16_t cmd = PciReadConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    PciWriteConfig16(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd | 0x06);
    
    // Get MMIO base address from BAR5
    uint64_t mmio_phys = pci_dev.bar0 & ~0xF;
    if (mmio_phys == 0) {
        // Try reading BAR5 directly
        mmio_phys = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, 0x24) & ~0xF;
    }
    
    if (mmio_phys == 0) {
        PrintKernel("AHCI: Invalid MMIO base address\n");
        return -1;
    }
    
    PrintKernel("AHCI: MMIO base: 0x");
    PrintKernelHex(mmio_phys);
    PrintKernel("\n");
    
    // Align physical address and size to page boundaries
    uint64_t mmio_phys_aligned = mmio_phys & ~0xFFF;
    uint64_t mmio_offset = mmio_phys - mmio_phys_aligned;
    uint64_t mmio_size_aligned = ((0x1100 + mmio_offset + 0xFFF) & ~0xFFF);
    
    PrintKernel("AHCI: Aligned MMIO: 0x");
    PrintKernelHex(mmio_phys_aligned);
    PrintKernel(" size: 0x");
    PrintKernelHex(mmio_size_aligned);
    PrintKernel(" offset: 0x");
    PrintKernelHex(mmio_offset);
    PrintKernel("\n");
    
    // Map MMIO region (following xHCI pattern)
    g_ahci_controller.mmio_size = mmio_size_aligned;
    volatile uint8_t* mmio_base_raw = (volatile uint8_t*)VMemAlloc(g_ahci_controller.mmio_size);
    if (!mmio_base_raw) {
        PrintKernel("AHCI: Failed to allocate virtual space\n");
        return -1;
    }
    
    // Unmap RAM pages
    if (VMemUnmap((uint64_t)mmio_base_raw, g_ahci_controller.mmio_size) != VMEM_SUCCESS) {
        PrintKernel("AHCI: Failed to unmap RAM pages\n");
        VMemFree((void*)mmio_base_raw, g_ahci_controller.mmio_size);
        return -1;
    }
    
    // Map MMIO
    uint64_t map_flags = PAGE_WRITABLE | PAGE_NOCACHE;
    if (VMemMapMMIO((uint64_t)mmio_base_raw, mmio_phys_aligned, g_ahci_controller.mmio_size, map_flags) != VMEM_SUCCESS) {
        PrintKernel("AHCI: Failed to map MMIO\n");
        return -1;
    }
    
    // Adjust base pointer to account for offset
    g_ahci_controller.mmio_base = mmio_base_raw + mmio_offset;
    
    __asm__ volatile("mfence" ::: "memory");
    
    PrintKernel("AHCI: MMIO mapped to 0x");
    PrintKernelHex((uint64_t)g_ahci_controller.mmio_base);
    PrintKernel("\n");
    
    // Test MMIO mapping
    uint32_t cap = AHCI_ReadReg(AHCI_CAP);
    if (cap == 0 || cap == 0xFFFFFFFF) {
        PrintKernel("AHCI: Invalid capabilities register\n");
        return -1;
    }
    
    PrintKernel("AHCI: Capabilities: 0x");
    PrintKernelHex(cap);
    PrintKernel("\n");
    
    // Enable AHCI mode
    uint32_t ghc = AHCI_ReadReg(AHCI_GHC);
    ghc |= AHCI_GHC_AE;
    AHCI_WriteReg(AHCI_GHC, ghc);
    
    // Get ports implemented
    g_ahci_controller.ports_implemented = AHCI_ReadReg(AHCI_PI);
    PrintKernel("AHCI: Ports implemented: 0x");
    PrintKernelHex(g_ahci_controller.ports_implemented);
    PrintKernel("\n");
    
    // Initialize active ports
    for (int i = 0; i < 32; i++) {
        if (!(g_ahci_controller.ports_implemented & (1 << i))) continue;
        
        // Check if device is present
        uint32_t ssts = AHCI_ReadPortReg(i, AHCI_PORT_SSTS);
        if ((ssts & AHCI_PORT_SSTS_DET_MASK) != AHCI_PORT_SSTS_DET_PRESENT) continue;
        
        PrintKernel("AHCI: Device detected on port ");
        PrintKernelInt(i);
        PrintKernel("\n");
        
        if (AHCI_InitPort(i) == 0) {
            PrintKernel("AHCI: Port ");
            PrintKernelInt(i);
            PrintKernel(" initialized successfully\n");
            
            // Register as block device
            const char* dev_name = GenerateDriveName(DEVICE_TYPE_AHCI);
            
            // Get actual sector count from IDENTIFY command
            uint64_t total_sectors = AHCI_GetDriveCapacity(i);
            
            PrintKernel("AHCI: Port ");
            PrintKernelInt(i);
            PrintKernel(" capacity: ");
            PrintKernelInt(total_sectors);
            PrintKernel(" sectors (");
            PrintKernelInt((total_sectors * 512) / (1024 * 1024));
            PrintKernel(" MB)\n");
            
            BlockDevice* dev = BlockDeviceRegister(
                DEVICE_TYPE_AHCI,
                512,
                total_sectors,
                dev_name,
                (void*)(uintptr_t)(i + 1), // Port number + 1 to avoid NULL
                (ReadBlocksFunc)AHCI_ReadBlocksWrapper,
                (WriteBlocksFunc)AHCI_WriteBlocksWrapper
            );
            
            if (dev) {
                PrintKernel("AHCI: Registered block device: ");
                PrintKernel(dev_name);
                PrintKernel("\n");
                BlockDeviceDetectAndRegisterPartitions(dev);
            }
        }
    }
    
    g_ahci_controller.initialized = 1;
    PrintKernelSuccess("AHCI: Driver initialized successfully\n");
    return 0;
}

const AHCIController* AHCI_GetController(void) {
    return g_ahci_controller.initialized ? &g_ahci_controller : NULL;
}

// Wrapper functions for BlockDevice integration
static int AHCI_ReadBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, void* buffer) {
    if (!device || !device->driver_data) {
        PrintKernel("AHCI: Invalid device or driver_data\n");
        return -1;
    }
    int port = (uintptr_t)device->driver_data - 1;
    
    int result = AHCI_ReadSectors(port, start_lba, count, buffer);
    if (result != 0) {
        PrintKernel("AHCI: Read failed with error ");
        PrintKernelInt(result);
        PrintKernel("\n");
    }
    return result;
}

static int AHCI_WriteBlocksWrapper(struct BlockDevice* device, uint64_t start_lba, uint32_t count, const void* buffer) {
    if (!device || !device->driver_data) return -1;
    int port = (uintptr_t)device->driver_data - 1;
    
    // AHCI_WriteSectors expects sectors, not blocks, but they're the same for 512-byte sectors
    return AHCI_WriteSectors(port, start_lba, count, buffer);
}