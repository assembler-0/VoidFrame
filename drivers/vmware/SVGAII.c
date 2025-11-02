#include <SVGAII.h>
#include <drivers/PCI/PCI.h>
#include <include/Io.h>
#include <mm/VMem.h>
#include <kernel/etc/Console.h>

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

bool SVGAII_DetectAndInitialize() {
    PciDevice pci_dev;

    PrintKernel("[SVGA] Detecting VMware SVGA II device...\n");
    PrintKernel("[SVGA] Looking for vendor 0x15AD, device 0x0405\n");

    // 1. Detect SVGA II PCI device
    if (!PciFindDevice(SVGAII_PCI_VENDOR_ID, SVGAII_PCI_DEVICE_ID, &pci_dev)) {
        PrintKernel("[SVGA] VMware SVGA II device 0x0405 not found\n");
        
        // Try alternative device IDs
        if (PciFindDevice(0x15AD, 0x0710, &pci_dev)) {
            PrintKernel("[SVGA] Found VMware SVGA 3D device (0x0710)\n");
        } else if (PciFindDevice(0x15AD, 0x0404, &pci_dev)) {
            PrintKernel("[SVGA] Found VMware SVGA device (0x0404)\n");
        } else {
            PrintKernel("[SVGA] No VMware SVGA device found\n");
            svgaII_device.initialized = false;
            return false;
        }
    }

    PrintKernel("[SVGA] Found VMware SVGA II at ");
    PrintKernelInt(pci_dev.bus); PrintKernel(":");
    PrintKernelInt(pci_dev.device); PrintKernel(".");
    PrintKernelInt(pci_dev.function); PrintKernel("\n");

    // 2. Get I/O port base from BAR0
    uint32_t bar0 = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, 0x10);
    if ((bar0 & 1) == 0) {
        PrintKernel("[SVGA] Memory-mapped I/O not supported\n");
        svgaII_device.initialized = false;
        return false;
    }

    svgaII_device.io_port_base = (uint16_t)(bar0 & ~0x3u);
    PrintKernel("[SVGA] I/O base: 0x");
    PrintKernelHex(svgaII_device.io_port_base);
    PrintKernel("\n");

    // 3. Enable PCI device
    uint32_t cmd = PciConfigReadDWord(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
    cmd |= 0x07; // Enable I/O space, memory space, and bus mastering
    PciConfigWriteDWord(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd);

    // 4. Negotiate SVGA ID
    SVGAII_WriteReg(SVGA_REG_ID, SVGA_ID_2);
    uint32_t id = SVGAII_ReadReg(SVGA_REG_ID);
    if (id != SVGA_ID_2) {
        SVGAII_WriteReg(SVGA_REG_ID, SVGA_ID_1);
        id = SVGAII_ReadReg(SVGA_REG_ID);
        if (id != SVGA_ID_1) {
            SVGAII_WriteReg(SVGA_REG_ID, SVGA_ID_0);
            id = SVGAII_ReadReg(SVGA_REG_ID);
            if (id != SVGA_ID_0) {
                PrintKernel("[SVGA] Could not negotiate SVGA ID\n");
                svgaII_device.initialized = false;
                return false;
            }
        }
    }

    PrintKernel("[SVGA] Negotiated SVGA ID: 0x");
    PrintKernelHex(id);
    PrintKernel("\n");

    // 5. Get framebuffer info
    uint32_t fb_start = SVGAII_ReadReg(SVGA_REG_FB_START);
    uint32_t fb_size = SVGAII_ReadReg(SVGA_REG_VRAM_SIZE);

    PrintKernel("[SVGA] Framebuffer: 0x");
    PrintKernelHex(fb_start);
    PrintKernel(" size: ");
    PrintKernelInt(fb_size / (1024*1024));
    PrintKernel("MB\n");

    // 6. Map framebuffer (using xHCI pattern)
    PrintKernel("[SVGA] Mapping framebuffer...\n");
    
    // Allocate virtual space
    uint64_t fb_size_aligned = (fb_size + 0xFFFULL) & ~0xFFFULL;
    void* fb_virt_base = VMemAlloc(fb_size_aligned);
    if (!fb_virt_base) {
        PrintKernel("[SVGA] Failed to allocate virtual space\n");
        svgaII_device.initialized = false;
        return false;
    }

    // Unmap the RAM pages that VMemAlloc mapped
    if (VMemUnmap((uint64_t)fb_virt_base, fb_size_aligned) != VMEM_SUCCESS) {
        PrintKernel("[SVGA] Failed to unmap RAM pages\n");
        VMemFree(fb_virt_base, fb_size_aligned);
        svgaII_device.initialized = false;
        return false;
    }
    
    // Map MMIO region
    uint64_t fb_phys_aligned = fb_start & ~0xFFFULL;
    if (VMemMapMMIO((uint64_t)fb_virt_base, fb_phys_aligned, fb_size_aligned, PAGE_WRITABLE | PAGE_NOCACHE) != VMEM_SUCCESS) {
        PrintKernel("[SVGA] Failed to map framebuffer MMIO\n");
        svgaII_device.initialized = false;
        return false;
    }
    
    uint64_t fb_off = fb_start - fb_phys_aligned;
    svgaII_device.framebuffer = (uint32_t*)((uint8_t*)fb_virt_base + fb_off);

    // framebuffer pointer set above
    svgaII_device.fb_size = fb_size;

    // 7. Check capabilities
    uint32_t caps = SVGAII_ReadReg(SVGA_REG_CAPABILITIES);
    PrintKernel("[SVGA] Capabilities: 0x");
    PrintKernelHex(caps);
    PrintKernel("\n");

    // 8. Enable SVGA and set mode
    SVGAII_WriteReg(SVGA_REG_ENABLE, 1);
    SVGAII_SetMode(800, 600, 32);

    svgaII_device.initialized = true;
    PrintKernelSuccess("[SVGA] VMware SVGA II initialized successfully\n");
    return true;
}

void SVGAII_SetMode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!svgaII_device.initialized) return;

    PrintKernel("[SVGA] Setting mode: ");
    PrintKernelInt(width); PrintKernel("x");
    PrintKernelInt(height); PrintKernel("x");
    PrintKernelInt(bpp); PrintKernel("\n");

    SVGAII_WriteReg(SVGA_REG_WIDTH, width);
    SVGAII_WriteReg(SVGA_REG_HEIGHT, height);
    SVGAII_WriteReg(SVGA_REG_BPP, bpp);

    // Update device info
    svgaII_device.width = width;
    svgaII_device.height = height;
    svgaII_device.bpp = bpp;
    svgaII_device.pitch = SVGAII_ReadReg(SVGA_REG_BYTES_PER_LINE);

    PrintKernel("[SVGA] Pitch: ");
    PrintKernelInt(svgaII_device.pitch);
    PrintKernel(" bytes per line\n");
}

void SVGAII_PutPixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!svgaII_device.initialized || x >= svgaII_device.width || y >= svgaII_device.height)
        return;

    if (svgaII_device.bpp == 32) {
        uint32_t* fb32 = svgaII_device.framebuffer;
        uint32_t stride_pixels = svgaII_device.pitch / 4;
        fb32[y * stride_pixels + x] = color;
    }
}

void SVGAII_UpdateScreen(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    if (!svgaII_device.initialized) return;

    // For basic implementation, just sync the display
    SVGAII_WriteReg(SVGA_REG_SYNC, 1);

    // Wait for sync to complete
    while (SVGAII_ReadReg(SVGA_REG_BUSY)) {
        // Busy wait
    }
}

void SVGAII_FillRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!svgaII_device.initialized) return;

    for (uint32_t j = y; j < y + height && j < svgaII_device.height; ++j) {
        for (uint32_t i = x; i < x + width && i < svgaII_device.width; ++i) {
            SVGAII_PutPixel(i, j, color);
        }
    }
    SVGAII_UpdateScreen(x, y, width, height);
}