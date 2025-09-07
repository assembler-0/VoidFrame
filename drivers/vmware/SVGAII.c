#include "SVGAII.h"
#include "../../drivers/PCI/PCI.h" // Assuming PCI.h is in the parent directory
#include "../../include/Io.h" // Assuming Io.h is in include/
#include "../../mm/VMem.h" // Assuming VMem.h is in mm/

// Global device info instance
SVGAII_DeviceInfo svgaII_device;

// Helper functions for SVGA II register access
static inline void SVGAII_WriteReg(uint16_t index, uint32_t value) {
    outl(svgaII_device.io_port_base + SVGA_INDEX, index);
    outl(svgaII_device.io_port_base + SVGA_VALUE, value);
}

static inline uint32_t SVGAII_ReadReg(uint16_t index) {
    outl(svgaII_device.io_port_base + SVGA_INDEX, index);
    return inl(svgaII_device.io_port_base + SVGA_VALUE);
}

// Function to detect and initialize the SVGA II device
bool SVGAII_DetectAndInitialize() {
    PciDevice pci_dev;

    // 1. Detect SVGA II PCI device
    if (!PciFindDevice(SVGAII_PCI_VENDOR_ID, SVGAII_PCI_DEVICE_ID, &pci_dev)) {
        // Device not found
        svgaII_device.initialized = false;
        return false;
    }

    // Store PCI info (bus, device, function) if needed later
    // pci_dev.bus, pci_dev.device, pci_dev.function

    // 2. Get I/O port base from BAR0
    // The OSDev Wiki states: "The base I/O port (SVGA_PORT_BASE) is not fixed and is derived by subtracting 1 from BAR0 in the PCI configuration."
    uint32_t bar0 = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_BAR0_REG);
    // I/O BAR: bit0=1 => I/O space; mask off low two bits per PCI spec
    if ((bar0 & 1) == 0) {
        // TODO: handle MMIO BAR0
    } else {
        svgaII_device.io_port_base = (uint16_t)(bar0 & ~0x3u);
    }

    uint32_t cmdsts = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND_REG);
    cmdsts |= (uint32_t)(PCI_CMD_MEM_SPACE_EN | PCI_CMD_BUS_MASTER_EN);
    PciConfigWriteDWord(pci_dev.bus, pci_dev.device, pci_dev.function, PCI_COMMAND_REG, cmdsts);

    // 3. Negotiate SVGA ID
    SVGAII_WriteReg(SVGA_REG_ID, SVGA_ID_2); // Try ID_2 first, as it's common
    if (SVGAII_ReadReg(SVGA_REG_ID) != SVGA_ID_2) {
        SVGAII_WriteReg(SVGA_REG_ID, SVGA_ID_0); // Fallback to ID_0
        if (SVGAII_ReadReg(SVGA_REG_ID) != SVGA_ID_0) {
            // Could not negotiate a supported ID
            svgaII_device.initialized = false;
            return false;
        }
    }

    // 4. Get Framebuffer and FIFO addresses and sizes
    uint32_t fb_phys_addr = SVGAII_ReadReg(SVGA_REG_FB_START);
    uint32_t fb_size = SVGAII_ReadReg(SVGA_REG_FB_SIZE);
    uint32_t fifo_phys_addr = SVGAII_ReadReg(SVGA_REG_FIFO_START);
    uint32_t fifo_size = SVGAII_ReadReg(SVGA_REG_FIFO_SIZE);

    uint64_t fb_phys_aligned = fb_phys_addr & ~0xFFFULL;
    uint64_t fb_off          = fb_phys_addr - fb_phys_aligned;
    uint64_t fb_size_aligned = (fb_off + fb_size + 0xFFFULL) & ~0xFFFULL;
    uint64_t fb_virt         = (uint64_t)PHYS_TO_VIRT(fb_phys_aligned);
    if (VMemMapMMIO(fb_virt, fb_phys_aligned, fb_size_aligned, VMEM_WRITE | VMEM_NOCACHE) != VMEM_SUCCESS) {
        svgaII_device.initialized = false;
        return false;
    }
    svgaII_device.framebuffer = (uint32_t*)(fb_virt + fb_off);
    svgaII_device.fb_size     = (uint32_t)fb_size_aligned;

    // FIFO mapping
    uint64_t fifo_phys_aligned = fifo_phys_addr & ~0xFFFULL;
    uint64_t fifo_off          = fifo_phys_addr - fifo_phys_aligned;
    uint64_t fifo_size_aligned = (fifo_off + fifo_size + 0xFFFULL) & ~0xFFFULL;
    uint64_t fifo_virt         = (uint64_t)PHYS_TO_VIRT(fifo_phys_aligned);
    if (VMemMapMMIO(fifo_virt, fifo_phys_aligned, fifo_size_aligned, VMEM_WRITE | VMEM_NOCACHE) != VMEM_SUCCESS) {
        svgaII_device.initialized = false;
        return false;
    }
    svgaII_device.fifo_ptr = (uint32_t*)(fifo_virt + fifo_off);
    svgaII_device.fifo_size = (uint32_t)fifo_size_aligned;

    // Initialize FIFO (as per OSDev Wiki, write 0 to FIFO_MIN and FIFO_MAX)
    // This part needs more specific FIFO initialization details from OSDev Wiki or other sources.
    // For now, a basic setup:
    svgaII_device.fifo_ptr[0] = 0; // FIFO_MIN
    svgaII_device.fifo_ptr[1] = fifo_size; // FIFO_MAX
    svgaII_device.fifo_ptr[2] = 0; // FIFO_NEXT_CMD
    svgaII_device.fifo_ptr[3] = 0; // FIFO_STOP

    // 6. Enable SVGA mode
    SVGAII_SetMode(800, 600, 32);

    svgaII_device.initialized = true;
    return true;
}

// Function to set display mode
void SVGAII_SetMode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!svgaII_device.initialized) return;

    SVGAII_WriteReg(SVGA_REG_WIDTH, width);
    SVGAII_WriteReg(SVGA_REG_HEIGHT, height);
    SVGAII_WriteReg(SVGA_REG_BPP, bpp);

    // Update device info
    svgaII_device.width = width;
    svgaII_device.height = height;
    svgaII_device.bpp = bpp;
    svgaII_device.pitch = width * (bpp / 8); // Assuming linear framebuffer

    // Trigger mode set (usually by writing to SVGA_REG_ENABLE again or a specific command)
    // OSDev Wiki suggests writing 1 to SVGA_REG_ENABLE after setting mode registers.
    SVGAII_WriteReg(SVGA_REG_ENABLE, 1);

    // Update the screen to reflect the new mode
    SVGAII_UpdateScreen(0, 0, width, height);
}

// Function to put a pixel (direct framebuffer access)
void SVGAII_PutPixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!svgaII_device.initialized || x >= svgaII_device.width || y >= svgaII_device.height) return;

    // Assuming 32-bit BPP for simplicity (color is ARGB)
    // Need to handle different BPP values (16, 24) if required.
    if (svgaII_device.bpp == 32) {
        uint32_t* fb32 = svgaII_device.framebuffer;
        uint32_t stride_pixels = svgaII_device.pitch / 4;
        fb32[y * stride_pixels + x] = color;
    }
    // Add logic for 16-bit, 24-bit if necessary
}

// Function to update a portion of the screen using FIFO command
void SVGAII_UpdateScreen(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!svgaII_device.initialized) return;

    // Ensure FIFO is ready for commands
    // This is a simplified FIFO write. A proper FIFO implementation needs to handle
    // FIFO head/tail, wrap-around, and waiting for space.
    // For now, assume enough space and direct write.
    uint32_t* fifo = svgaII_device.fifo_ptr;
    uint32_t next_cmd_offset = fifo[2]; // FIFO_NEXT_CMD

    fifo[next_cmd_offset / 4] = SVGA_CMD_UPDATE;
    fifo[(next_cmd_offset + 4) / 4] = x;
    fifo[(next_cmd_offset + 8) / 4] = y;
    fifo[(next_cmd_offset + 12) / 4] = width;
    fifo[(next_cmd_offset + 16) / 4] = height;

    fifo[2] = (next_cmd_offset + 20) % svgaII_device.fifo_size; // Update FIFO_NEXT_CMD

    // Tell the device there are new commands
    outl(svgaII_device.io_port_base + SVGA_BIOS, 0); // Write anything to SVGA_BIOS to kick the FIFO
}

// Function to fill a rectangle (direct framebuffer access and then update)
void SVGAII_FillRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!svgaII_device.initialized) return;

    // Simple fill by iterating pixels. Can be optimized with memset/memcpy for larger areas.
    for (uint32_t j = y; j < y + height; ++j) {
        for (uint32_t i = x; i < x + width; ++i) {
            SVGAII_PutPixel(i, j, color);
        }
    }
    SVGAII_UpdateScreen(x, y, width, height);
}
