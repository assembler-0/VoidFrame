#include "xHCI.h"
#include "Console.h"
#include "MemOps.h"
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


// A helper function to read/write to our controller's registers
static inline uint32_t xhci_read_reg(volatile uint32_t* reg) {
    return *reg;
}

static inline void xhci_write_reg(volatile uint32_t* reg, uint32_t value) {
    *reg = value;
}


int xhci_controller_init(XhciController* controller, const PciDevice* pci_dev) {
    PrintKernel("xHCI: Initializing controller...\n");
    controller->pci_device = *pci_dev;

    uint32_t bar0 = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, 0x10);
    uint32_t bar1 = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, 0x14);
    uint32_t mmio_physical_base = bar0 & 0xFFFFFFF0;

    PrintKernel("xHCI: Found MMIO physical base at 0x"); PrintKernelHex(mmio_physical_base); PrintKernel("\n");

    controller->mmio_base = VMemAlloc(PAGE_SIZE);
    FastMemset((void*)controller->mmio_base, 0, PAGE_SIZE);

    // --- STEP 2: Enable Bus Mastering in PCI Config ---
    uint32_t pci_command = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, 0x04);
    pci_command |= (1 << 2); // Set the Bus Master Enable bit
    PciConfigWriteDWord(pci_dev->bus, pci_dev->device, pci_dev->function, 0x04, pci_command);

    controller->operational_regs = (volatile uint32_t*)(controller->mmio_base + 0x00);

    // Make sure the controller is halted before we reset
    uint32_t status = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
    if ((status & USBSTS_HC_HALTED) == 0) {
        PrintKernel("xHCI: Controller not halted. Attempting to stop...\n");
        xhci_write_reg(&controller->operational_regs[XHCI_OP_USBCMD / 4], 0);
        // Wait for it to halt (with a timeout)
        int timeout = 500; // 500ms timeout
        while (timeout > 0) {
            status = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
            if (status & USBSTS_HC_HALTED) break;
            // You need a sleep/delay function here!
            delay(1000);
            timeout--;
        }
        if (timeout == 0) {
            PrintKernel("xHCI: FATAL - Controller failed to halt!\n");
            return -1;
        }
    }
    PrintKernel("xHCI: Controller is halted.\n");

    // Issue the reset command
    PrintKernel("xHCI: Resetting controller...\n");
    xhci_write_reg(&controller->operational_regs[XHCI_OP_USBCMD / 4], USBCMD_HC_RESET);

    // Wait for the reset bit to be cleared by the hardware (with a timeout)
    int timeout = 1000; // 1 second timeout
    while (timeout > 0) {
        uint32_t cmd = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBCMD / 4]);
        if ((cmd & USBCMD_HC_RESET) == 0) break;
        delay(1000);
        timeout--;
    }
    if (timeout == 0) {
        PrintKernel("xHCI: FATAL - Controller reset timed out!\n");
        return -1;
    }
    PrintKernel("xHCI: Controller reset complete.\n");

    // The spec says we must also wait until the Controller Not Ready (CNR)
    // bit in USBSTS is 0.
    timeout = 1000; // 1 second timeout
    while (timeout > 0) {
        status = xhci_read_reg(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
        if ((status & USBSTS_CTRL_RDY) == 0) break;
        delay(1000);
        timeout--;
    }
    if (timeout == 0) {
        PrintKernel("xHCI: FATAL - Controller not ready after reset!\n");
        return -1;
    }
    PrintKernel("xHCI: Controller is ready for setup.\n");


    PrintKernel("xHCI: Phase 1 initialization complete!\n");
    return 0; // Success!
}

void xhci_init() {
    PciDevice xhci_pci_dev;
    if (PciFindByClass(0x0C, 0x03, 0x30, &xhci_pci_dev) == 0) {
        PrintKernelSuccess("[SYSTEM] Found an xHCI controller!\n");

        XhciController my_xhci_controller;
        if (xhci_controller_init(&my_xhci_controller, &xhci_pci_dev) == 0) {
            PrintKernelSuccess("[SYSTEM] xHCI driver phase 1 succeeded!\n");
        } else {
            PrintKernelError("[SYSTEM] xHCI driver phase 1 failed!\n");
        }

    } else {
        PrintKernel("[SYSTEM] No xHCI controller found on the system.\n");
    }
}
