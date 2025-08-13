#include "xHCI.h"
#include "Console.h"
#include "VMem.h"
#include "VesaBIOSExtension.h"

// 5.4 - Operational Registers
#define XHCI_OP_USBCMD  0x00 // USB Command Register
#define XHCI_OP_USBSTS  0x04 // USB Status Register
#define XHCI_OP_CONFIG  0x38 // Configure Register
#define XHCI_OP_DCBAAP  0x30 // Device Context Base Address Array Pointer

// 5.5 - Runtime Registers
#define XHCI_RT_MFINDEX 0x00 // Microframe Index

// --- Bitmasks for Registers ---
#define USBCMD_RUN_STOP     (1 << 0) // Run/Stop bit
#define USBCMD_HC_RESET     (1 << 1) // Host Controller Reset bit
#define USBSTS_HC_HALTED    (1 << 0) // Host Controller Halted bit
#define USBSTS_CTRL_RDY     (1 << 11) // Controller Not Ready bit (CNR)

#define XHCI_CAP_CAPLENGTH  0x00  // Capability Register Length
#define XHCI_CAP_HCIVERSION 0x02  // Host Controller Interface Version
#define XHCI_CAP_HCSPARAMS1 0x04  // Structural Parameters 1
#define XHCI_CAP_RTSOFF     0x18  // Runtime Register Space Offset

// A helper function to read/write to our controller's registers
static inline uint32_t xhci_read_reg(volatile uint32_t* reg) {
    return *reg;
}

static inline void xhci_write_reg(volatile uint32_t* reg, uint32_t value) {
    *reg = value;
}

// Updated initialization function
int xHCIControllerInit(XhciController* controller, const PciDevice* pci_dev) {
    PrintKernel("xHCI: Starting initialization for controller at B:D:F ");
    PrintKernelHex(pci_dev->bus); PrintKernel(":");
    PrintKernelHex(pci_dev->device); PrintKernel(":");
    PrintKernelHex(pci_dev->function); PrintKernel("\n");

    controller->pci_device = *pci_dev;

    // --- Step 1: Enable PCI Bus Mastering and Memory Space ---
    PrintKernel("xHCI: Enabling Bus Mastering and Memory Space...\n");
    uint32_t pci_command = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, PCI_COMMAND_REG);
    pci_command |= (PCI_CMD_MEM_SPACE_EN | PCI_CMD_BUS_MASTER_EN);
    PciConfigWriteDWord(pci_dev->bus, pci_dev->device, pci_dev->function, PCI_COMMAND_REG, pci_command);

    // --- Step 2: Discover the 64-bit MMIO Physical Base Address ---
    uint64_t mmio_physical_base = 0;
    uint32_t bar_for_size_calc = 0;
    int found_bar = 0;

    // xHCI controllers use a 64-bit memory BAR. We must find it.
    for (int i = 0; i < 6; i++) {
        uint32_t bar_low = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, PCI_BAR0_REG + (i * 4));

        // Check if it's a memory BAR (Bit 0 is 0)
        if ((bar_low & 0x1) == 0) {
            uint8_t type = (bar_low >> 1) & 0x3;
            if (type == 0x2) { // Type is 0b10, indicating a 64-bit BAR
                PrintKernel("xHCI: Found 64-bit BAR at index "); PrintKernelInt(i); PrintKernel("\n");
                uint32_t bar_high = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, PCI_BAR0_REG + ((i + 1) * 4));
                mmio_physical_base = ((uint64_t)bar_high << 32) | (bar_low & 0xFFFFFFF0);
                bar_for_size_calc = bar_low;
                found_bar = 1;
                break; // Exit loop once we've found our BAR
            }
        }
    }

    if (!found_bar) {
        PrintKernelError("xHCI: FATAL - No 64-bit memory BAR found!\n");
        return -1;
    }
    PrintKernel("xHCI: Physical MMIO Base Address: 0x"); PrintKernelHex(mmio_physical_base); PrintKernel("\n");


    uint64_t mmio_size = GetPCIMMIOSize(&controller->pci_device, bar_for_size_calc);
    PrintKernel("xHCI: MMIO size: 0x"); PrintKernelHex(mmio_size); PrintKernel("\n");

    // Store the size in the controller for later cleanup
    controller->mmio_size = mmio_size;

    controller->mmio_base = VMemAlloc(mmio_size);
    if (!controller->mmio_base) {
        PrintKernelError("xHCI: Failed to allocate virtual space for MMIO\n");
        return -1;
    }

    // Unmap the RAM pages that VMemAlloc mapped
    PrintKernel("xHCI: Unmapping RAM pages before MMIO mapping...\n");
    int unmap_result = VMemUnmap((uint64_t)controller->mmio_base, mmio_size);
    if (unmap_result != VMEM_SUCCESS) {
        PrintKernelError("xHCI: Failed to unmap RAM pages\n");
        VMemFree(controller->mmio_base, mmio_size);
        return -1;
    }

    uint64_t map_flags = PAGE_WRITABLE | PAGE_NOCACHE;
    // Now map the MMIO region to the virtual addresses
    int map_result = VMemMapMMIO((uint64_t)controller->mmio_base, mmio_physical_base, mmio_size, map_flags);
    if (map_result != VMEM_SUCCESS) {
        PrintKernelError("xHCI: FATAL - VMemMapMMIO failed with code ");
        PrintKernelInt(map_result); PrintKernel("\n");
        // Don't call VMemFree here since we unmapped the pages manually
        return -1;
    }

    // Add a memory barrier to ensure mapping is complete
    __asm__ volatile("mfence" ::: "memory");

    PrintKernel("xHCI: Successfully mapped MMIO to virtual address: 0x");
    PrintKernelHex((uint64_t)controller->mmio_base); PrintKernel("\n");

    // Read important controller limits and features
    uint32_t hcsparams1 = read_cap_reg(XHCI_CAP_HCSPARAMS1);
    controller->max_slots = (hcsparams1 & 0xFF);           // How many devices can connect
    controller->max_ports = (hcsparams1 >> 24) & 0xFF;     // How many USB ports
    controller->max_intrs = (hcsparams1 >> 8) & 0x7FF;     // Max interrupt sources

    PrintKernel("xHCI: Max slots: "); PrintKernelInt(controller->max_slots);
    PrintKernel( " Max ports: "); PrintKernelInt(controller->max_ports);
    PrintKernel(" Max intrs: "); PrintKernelInt(controller->max_intrs); PrintKernel("\n");

    // --- Step 4: Verify the mapping works before proceeding ---
    // Test read a known register to verify mapping
    PrintKernel("xHCI: Testing MMIO mapping...\n");
    volatile uint8_t* mmio = controller->mmio_base;

    // Read capability length - this should never be 0x00 or 0xFF for a working xHCI
    uint8_t cap_length = mmio[XHCI_CAP_CAPLENGTH];
    uint16_t hci_version = *((volatile uint16_t*)(mmio + XHCI_CAP_HCIVERSION));
    uint32_t rts_offset = *((volatile uint32_t*)(mmio + XHCI_CAP_RTSOFF));

    PrintKernel("xHCI: Raw read test - CAPLENGTH = 0x"); PrintKernelHex(cap_length); PrintKernel("\n");
    PrintKernel("xHCI: Raw read test - HCIVERSION = 0x"); PrintKernelHex(hci_version); PrintKernel("\n");
    PrintKernel("xHCI: Raw read test - RTSOFF = 0x"); PrintKernelHex(rts_offset); PrintKernel("\n");

    // Check for invalid values which indicate mapping failure
    if (cap_length == 0x00 || cap_length == 0xFF || cap_length > 0x40) {
        PrintKernelError("xHCI: FATAL - CAPLENGTH invalid (0x");
        PrintKernelHex(cap_length);
        PrintKernel("). MMIO mapping failed.\n");

        // Additional debugging - try reading multiple locations
        PrintKernel("xHCI: Debug - First 16 bytes of MMIO:\n");
        for (int i = 0; i < 16; i++) {
            PrintKernel("  ["); PrintKernelHex(i); PrintKernel("] = 0x");
            PrintKernelHex(mmio[i]); PrintKernel("\n");
        }

        VMemFree((void*)controller->mmio_base, mmio_size);
        return -1;
    }

    // Verify HCI version makes sense (should be 0x0100, 0x0110, etc.)
    if (hci_version < 0x0100 || hci_version > 0x0120) {
        PrintKernelWarning("xHCI: Warning - Unusual HCI version: 0x");
        PrintKernelHex(hci_version); PrintKernel("\n");
    }

    controller->operational_regs = (volatile uint32_t*)(mmio + cap_length);
    controller->runtime_regs = (volatile uint32_t*)(mmio + (rts_offset & 0xFFFFFFE0));

    PrintKernel("xHCI: MMIO mapping verified successfully!\n");
    PrintKernel("xHCI: Operational Regs at VAddr: 0x");
    PrintKernelHex((uint64_t)controller->operational_regs); PrintKernel("\n");

    // --- Step 5: Halt, Reset, and Wait for the Controller ---
    const int TIMEOUT_MS = 1000;
    int timeout;

    // Make sure the controller is halted before we reset
    uint32_t status = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
    if ((status & USBSTS_HC_HALTED) == 0) {
        PrintKernel("xHCI: Controller not halted. Attempting to stop...\n");
        xhci_write_reg(&controller->operational_regs[XHCI_OP_USBCMD / 4], 0);

        timeout = TIMEOUT_MS;
        while (timeout > 0) {
            status = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
            if (status & USBSTS_HC_HALTED) break;
            delay(1000); // delay 1ms
            timeout--;
        }
        if (timeout == 0) {
            PrintKernelError("xHCI: FATAL - Controller failed to halt!\n");
            return -1;
        }
    }

    PrintKernel("xHCI: Resetting controller...\n");
    xhci_write_reg(&controller->operational_regs[XHCI_OP_USBCMD / 4], USBCMD_HC_RESET);
    timeout = TIMEOUT_MS;
    while (timeout > 0) {
        uint32_t cmd = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBCMD / 4]);
        if ((cmd & USBCMD_HC_RESET) == 0) break;
        delay(1000);
        timeout--;
    }
    if (timeout == 0) {
        PrintKernelError("xHCI: FATAL - Controller reset timed out!\n");
        return -1;
    }

    PrintKernel("xHCI: Waiting for controller to be ready...\n");
    timeout = TIMEOUT_MS;
    while (timeout > 0) {
        status = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
        if ((status & USBSTS_CTRL_RDY) == 0) break;
        delay(1000);
        timeout--;
    }
    if (timeout == 0) {
        PrintKernelError("xHCI: FATAL - Controller not ready after reset!\n");
        return -1;
    }

    PrintKernelSuccess("xHCI: Controller is ready for setup.\n");
    PrintKernelSuccess("xHCI: Phase 1 initialization complete!\n");
    return 0; // Success!
}

void xHCIControllerCleanup(XhciController* controller) {
    if (controller->mmio_base) {
        VMemUnmapMMIO((uint64_t)controller->mmio_base);
        controller->mmio_base = NULL;
    }
}

void xHCIInit() {
    PciDevice xhci_pci_dev;
    if (PciFindByClass(0x0C, 0x03, 0x30, &xhci_pci_dev) == 0) {
        PrintKernelSuccess("xHCI: Found an xHCI controller!\n");

        XhciController controller;
        if (xHCIControllerInit(&controller, &xhci_pci_dev) == 0) {
            PrintKernelSuccess("xHCI: xHCI driver phase 1 succeeded!\n");
        } else {
            PrintKernelError("xHCI: xHCI driver phase 1 failed!\n");
        }

    } else {
        PrintKernel("xHCI: No xHCI controller found on the system.\n");
    }
}