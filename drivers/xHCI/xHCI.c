#include "xHCI.h"
#include "../../mm/MemOps.h"
#include "../../mm/VMem.h"
#include "Console.h"
#include "Cpu.h"

// 5.4 - Operational Registers
#define XHCI_OP_USBCMD  0x00 // USB Command Register
#define XHCI_OP_USBSTS  0x04 // USB Status Register
#define XHCI_OP_PAGESIZE 0x08 // Page Size Register
#define XHCI_OP_DNCTRL  0x14 // Device Notification Control Register
#define XHCI_OP_CRCR    0x18 // Command Ring Control Register
#define XHCI_OP_DCBAAP  0x30 // Device Context Base Address Array Pointer
#define XHCI_OP_CONFIG  0x38 // Configure Register

// 5.5 - Runtime Registers
#define XHCI_RT_MFINDEX 0x00 // Microframe Index
#define XHCI_RT_IR0     0x20 // Interrupter Register Set 0

// Port Registers (starting at operational base + 0x400)
#define XHCI_PORT_SC    0x00 // Port Status and Control
#define XHCI_PORT_PMSC  0x04 // Port Power Management Status and Control
#define XHCI_PORT_LI    0x08 // Port Link Info
#define XHCI_PORT_HLC   0x0C // Port Hardware LPM Control

// --- Bitmasks for Registers ---
#define USBCMD_RUN_STOP     (1 << 0) // Run/Stop bit
#define USBCMD_HC_RESET     (1 << 1) // Host Controller Reset bit
#define USBSTS_HC_HALTED    (1 << 0) // Host Controller Halted bit
#define USBSTS_CTRL_RDY     (1 << 11) // Controller Not Ready bit (CNR)

// Port Status and Control bits
#define PORTSC_CCS          (1 << 0)  // Current Connect Status
#define PORTSC_PED          (1 << 1)  // Port Enabled/Disabled
#define PORTSC_PR           (1 << 4)  // Port Reset
#define PORTSC_PP           (1 << 9)  // Port Power
#define PORTSC_SPEED_MASK   (0xF << 10) // Port Speed
#define PORTSC_CSC          (1 << 17) // Connect Status Change
#define PORTSC_PEC          (1 << 18) // Port Enabled/Disabled Change
#define PORTSC_PRC          (1 << 21) // Port Reset Change

// TRB Types
#define TRB_TYPE_NORMAL     1
#define TRB_TYPE_SETUP      2
#define TRB_TYPE_DATA       3
#define TRB_TYPE_STATUS     4
#define TRB_TYPE_LINK       6
#define TRB_TYPE_EVENT_DATA 7
#define TRB_TYPE_NOOP       8

// TRB Control bits
#define TRB_CYCLE_BIT       (1 << 0)
#define TRB_IOC             (1 << 5)  // Interrupt on Completion
#define TRB_IDT             (1 << 6)  // Immediate Data

#define XHCI_CAP_CAPLENGTH  0x00  // Capability Register Length
#define XHCI_CAP_HCIVERSION 0x02  // Host Controller Interface Version
#define XHCI_CAP_HCSPARAMS1 0x04  // Structural Parameters 1
#define XHCI_CAP_HCSPARAMS2 0x08  // Structural Parameters 2
#define XHCI_CAP_HCSPARAMS3 0x0C  // Structural Parameters 3
#define XHCI_CAP_HCCPARAMS1 0x10  // Capability Parameters 1
#define XHCI_CAP_RTSOFF     0x18  // Runtime Register Space Offset

// Ring sizes
#define COMMAND_RING_SIZE   256
#define EVENT_RING_SIZE     256
#define TRANSFER_RING_SIZE  256

// A helper function to read/write to our controller's registers
static inline uint32_t xHCIReadRegister(volatile uint32_t* reg) {
    return *reg;
}

static inline void xHCIWriteRegister(volatile uint32_t* reg, uint32_t value) {
    *reg = value;
}

static inline uint64_t xHCIReadRegister64(volatile uint64_t* reg) {
    return *reg;
}

static inline void xHCIWriteRegister64(volatile uint64_t* reg, uint64_t value) {
    *reg = value;
}

// Ring management functions
static int xHCIInitCommandRing(XhciController* controller) {
    PrintKernel("xHCI: Initializing command ring...\n");
    
    // Allocate physically contiguous memory for command ring
    controller->command_ring = VMemAlloc(COMMAND_RING_SIZE * sizeof(XhciTRB));
    if (!controller->command_ring) {
        PrintKernelError("xHCI: Failed to allocate command ring\n");
        return -1;
    }
    
    // Clear the ring
    for (int i = 0; i < COMMAND_RING_SIZE; i++) {
        controller->command_ring[i].parameter_lo = 0;
        controller->command_ring[i].parameter_hi = 0;
        controller->command_ring[i].status = 0;
        controller->command_ring[i].control = 0;
    }
    
    // Set up the link TRB at the end to make it circular
    XhciTRB* link_trb = &controller->command_ring[COMMAND_RING_SIZE - 1];
    uint64_t ring_phys = VIRT_TO_PHYS((uint64_t)controller->command_ring);
    link_trb->parameter_lo = (uint32_t)(ring_phys & 0xFFFFFFFF);
    link_trb->parameter_hi = (uint32_t)(ring_phys >> 32);
    link_trb->control = (TRB_TYPE_LINK << 10) | TRB_CYCLE_BIT;
    
    controller->command_ring_enqueue = 0;
    controller->command_ring_cycle = 1;
    
    // Set the Command Ring Control Register
    xHCIWriteRegister64((volatile uint64_t*)&controller->operational_regs[XHCI_OP_CRCR / 4], ring_phys | 1);
    
    PrintKernel("xHCI: Command ring initialized at physical address 0x");
    PrintKernelHex(ring_phys); PrintKernel("\n");
    return 0;
}

static int xHCIInitEventRing(XhciController* controller) {
    PrintKernel("xHCI: Initializing event ring...\n");
    
    // Allocate event ring
    controller->event_ring = VMemAlloc(EVENT_RING_SIZE * sizeof(XhciTRB));
    if (!controller->event_ring) {
        PrintKernelError("xHCI: Failed to allocate event ring\n");
        return -1;
    }
    
    // Clear the ring
    for (int i = 0; i < EVENT_RING_SIZE; i++) {
        controller->event_ring[i].parameter_lo = 0;
        controller->event_ring[i].parameter_hi = 0;
        controller->event_ring[i].status = 0;
        controller->event_ring[i].control = 0;
    }
    
    // Allocate Event Ring Segment Table
    controller->erst = VMemAlloc(sizeof(XhciERSTEntry));
    if (!controller->erst) {
        PrintKernelError("xHCI: Failed to allocate ERST\n");
        VMemFree(controller->event_ring, EVENT_RING_SIZE * sizeof(XhciTRB));
        return -1;
    }
    
    // Set up ERST entry
    uint64_t event_ring_phys = VIRT_TO_PHYS((uint64_t)controller->event_ring);
    controller->erst->address = event_ring_phys;
    controller->erst->size = EVENT_RING_SIZE;
    controller->erst->reserved = 0;
    
    controller->event_ring_dequeue = 0;
    controller->event_ring_cycle = 1;
    
    // Configure interrupter 0
    volatile uint32_t* interrupter = &controller->runtime_regs[XHCI_RT_IR0 / 4];
    xHCIWriteRegister(&interrupter[0], 0x3); // Enable interrupt and set interval
    xHCIWriteRegister(&interrupter[1], 1);   // ERST size = 1
    
    // Set ERST base address
    uint64_t erst_phys = VIRT_TO_PHYS((uint64_t)controller->erst);
    xHCIWriteRegister64((volatile uint64_t*)&interrupter[2], erst_phys);
    
    // Set event ring dequeue pointer
    xHCIWriteRegister64((volatile uint64_t*)&interrupter[4], event_ring_phys);
    
    PrintKernel("xHCI: Event ring initialized\n");
    return 0;
}

static int xHCIInitDeviceContextBaseArray(XhciController* controller) {
    PrintKernel("xHCI: Initializing Device Context Base Address Array...\n");
    
    // Allocate DCBAA - array of pointers to device contexts
    size_t dcbaa_size = (controller->max_slots + 1) * sizeof(uint64_t);
    controller->dcbaa = VMemAlloc(dcbaa_size);
    if (!controller->dcbaa) {
        PrintKernelError("xHCI: Failed to allocate DCBAA\n");
        return -1;
    }
    
    // Clear the array
    for (uint32_t i = 0; i <= controller->max_slots; i++) {
        controller->dcbaa[i] = 0;
    }
    
    // Set the DCBAAP register
    uint64_t dcbaa_phys = VIRT_TO_PHYS((uint64_t)controller->dcbaa);
    xHCIWriteRegister64((volatile uint64_t*)&controller->operational_regs[XHCI_OP_DCBAAP / 4], dcbaa_phys);
    
    PrintKernel("xHCI: DCBAA initialized at physical address 0x");
    PrintKernelHex(dcbaa_phys); PrintKernel("\n");
    return 0;
}

static void xHCIScanPorts(XhciController* controller) {
    PrintKernel("xHCI: Scanning ports...\n");
    
    volatile uint32_t* port_regs = (volatile uint32_t*)((uint8_t*)controller->operational_regs + 0x400);
    
    for (uint32_t port = 0; port < controller->max_ports; port++) {
        volatile uint32_t* port_sc = &port_regs[port * 4];
        uint32_t status = xHCIReadRegister(port_sc);
        
        PrintKernel("xHCI: Port "); PrintKernelInt(port + 1); PrintKernel(": ");
        
        if (status & PORTSC_CCS) {
            PrintKernel("Device connected");
            uint32_t speed = (status & PORTSC_SPEED_MASK) >> 10;
            PrintKernel(" (Speed: "); PrintKernelInt(speed); PrintKernel(")");
            
            // Power on port if not already powered
            if (!(status & PORTSC_PP)) {
                PrintKernel(" - Powering on port");
                xHCIWriteRegister(port_sc, status | PORTSC_PP);
            }
            
            // Reset port to enable it
            if (!(status & PORTSC_PED)) {
                PrintKernel(" - Resetting port");
                xHCIWriteRegister(port_sc, status | PORTSC_PR);
                
                // Wait for reset to complete
                int timeout = 100;
                while (timeout > 0) {
                    status = xHCIReadRegister(port_sc);
                    if (!(status & PORTSC_PR)) break;
                    delay(1000);
                    timeout--;
                }
                
                if (status & PORTSC_PED) {
                    PrintKernel(" - Port enabled");
                } else {
                    PrintKernel(" - Port enable failed");
                }
            }
        } else {
            PrintKernel("No device");
        }
        PrintKernel("\n");
    }
}

static int xHCIStartController(XhciController* controller) {
    PrintKernel("xHCI: Starting controller...\n");
    
    // Set the number of device slots
    uint32_t config = xHCIReadRegister(&controller->operational_regs[XHCI_OP_CONFIG / 4]);
    config = (config & ~0xFF) | controller->max_slots;
    xHCIWriteRegister(&controller->operational_regs[XHCI_OP_CONFIG / 4], config);
    
    // Start the controller
    uint32_t cmd = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBCMD / 4]);
    cmd |= USBCMD_RUN_STOP;
    xHCIWriteRegister(&controller->operational_regs[XHCI_OP_USBCMD / 4], cmd);
    
    // Wait for controller to start
    int timeout = 1000;
    while (timeout > 0) {
        uint32_t status = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
        if (!(status & USBSTS_HC_HALTED)) break;
        delay(1000);
        timeout--;
    }
    
    if (timeout == 0) {
        PrintKernelError("xHCI: Controller failed to start\n");
        return -1;
    }
    
    PrintKernelSuccess("xHCI: Controller started successfully\n");
    return 0;
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
    volatile uint8_t* mmio = controller->mmio_base;
    uint32_t hcsparams1 = *((volatile uint32_t*)(mmio + XHCI_CAP_HCSPARAMS1));
    controller->max_slots = (hcsparams1 & 0xFF);           // How many devices can connect
    controller->max_ports = (hcsparams1 >> 24) & 0xFF;     // How many USB ports
    controller->max_intrs = (hcsparams1 >> 8) & 0x7FF;     // Max interrupt sources

    PrintKernel("xHCI: Max slots: "); PrintKernelInt(controller->max_slots);
    PrintKernel( " Max ports: "); PrintKernelInt(controller->max_ports);
    PrintKernel(" Max intrs: "); PrintKernelInt(controller->max_intrs); PrintKernel("\n");

    // --- Step 4: Verify the mapping works before proceeding ---
    // Test read a known register to verify mapping
    PrintKernel("xHCI: Testing MMIO mapping...\n");

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
        PrintKernel("xHCI: Warning - Unusual HCI version: 0x");
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
    uint32_t status = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
    if ((status & USBSTS_HC_HALTED) == 0) {
        PrintKernel("xHCI: Controller not halted. Attempting to stop...\n");
        xHCIWriteRegister(&controller->operational_regs[XHCI_OP_USBCMD / 4], 0);

        timeout = TIMEOUT_MS;
        while (timeout > 0) {
            status = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
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
    xHCIWriteRegister(&controller->operational_regs[XHCI_OP_USBCMD / 4], USBCMD_HC_RESET);
    timeout = TIMEOUT_MS;
    while (timeout > 0) {
        uint32_t cmd = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBCMD / 4]);
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
        status = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
        if ((status & USBSTS_CTRL_RDY) == 0) break;
        delay(1000);
        timeout--;
    }
    if (timeout == 0) {
        PrintKernelError("xHCI: FATAL - Controller not ready after reset!\n");
        return -1;
    }

    PrintKernelSuccess("xHCI: Controller is ready for setup.\n");

    // --- Phase 2: Initialize data structures ---
    PrintKernel("xHCI: Starting Phase 2 - Data structure initialization...\n");

    // Initialize command ring
    if (xHCIInitCommandRing(controller) != 0) {
        PrintKernelError("xHCI: Failed to initialize command ring\n");
        return -1;
    }

    // Initialize event ring
    if (xHCIInitEventRing(controller) != 0) {
        PrintKernelError("xHCI: Failed to initialize event ring\n");
        return -1;
    }

    // Initialize Device Context Base Address Array
    if (xHCIInitDeviceContextBaseArray(controller) != 0) {
        PrintKernelError("xHCI: Failed to initialize DCBAA\n");
        return -1;
    }

    // Start the controller
    if (xHCIStartController(controller) != 0) {
        PrintKernelError("xHCI: Failed to start controller\n");
        return -1;
    }

    // Scan for connected devices and enumerate them
    xHCIScanAndEnumeratePorts(controller);

    PrintKernelSuccess("xHCI: Full initialization complete!\n");
    return 0; // Success!
}

void xHCIControllerCleanup(XhciController* controller) {
    if (controller->mmio_base) {
        // Stop the controller first
        xHCIWriteRegister(&controller->operational_regs[XHCI_OP_USBCMD / 4], 0);
        
        // Wait for halt
        int timeout = 1000;
        while (timeout > 0) {
            uint32_t status = xHCIReadRegister(&controller->operational_regs[XHCI_OP_USBSTS / 4]);
            if (status & USBSTS_HC_HALTED) break;
            delay(1000);
            timeout--;
        }
    }
    
    // Free allocated structures
    if (controller->command_ring) {
        VMemFree(controller->command_ring, COMMAND_RING_SIZE * sizeof(XhciTRB));
        controller->command_ring = NULL;
    }
    
    if (controller->event_ring) {
        VMemFree(controller->event_ring, EVENT_RING_SIZE * sizeof(XhciTRB));
        controller->event_ring = NULL;
    }
    
    if (controller->erst) {
        VMemFree(controller->erst, sizeof(XhciERSTEntry));
        controller->erst = NULL;
    }
    
    if (controller->dcbaa) {
        size_t dcbaa_size = (controller->max_slots + 1) * sizeof(uint64_t);
        VMemFree(controller->dcbaa, dcbaa_size);
        controller->dcbaa = NULL;
    }
    
    if (controller->mmio_base) {
        VMemUnmapMMIO((uint64_t)controller->mmio_base, controller->mmio_size);
        controller->mmio_base = NULL;
    }
}

// Command ring management
static void xHCIRingCommandDoorbell(XhciController* controller) {
    // Ring doorbell for command ring (doorbell 0)
    volatile uint32_t* doorbell_regs = (volatile uint32_t*)((uint8_t*)controller->operational_regs + 0x400 + (controller->max_ports * 0x10));
    xHCIWriteRegister(&doorbell_regs[0], 0);
}

static int xHCISubmitCommand(XhciController* controller, XhciTRB* trb) {
    // Check if ring is full
    uint32_t next_enqueue = (controller->command_ring_enqueue + 1) % (COMMAND_RING_SIZE - 1);
    
    // Copy TRB to command ring
    XhciTRB* cmd_trb = &controller->command_ring[controller->command_ring_enqueue];
    cmd_trb->parameter_lo = trb->parameter_lo;
    cmd_trb->parameter_hi = trb->parameter_hi;
    cmd_trb->status = trb->status;
    cmd_trb->control = trb->control | (controller->command_ring_cycle ? TRB_CYCLE_BIT : 0);
    
    // Advance enqueue pointer
    controller->command_ring_enqueue = next_enqueue;
    if (controller->command_ring_enqueue == 0) {
        controller->command_ring_cycle = !controller->command_ring_cycle;
    }
    
    // Ring doorbell
    xHCIRingCommandDoorbell(controller);
    
    return 0;
}

// Process events from the event ring
static void xHCIProcessEvents(XhciController* controller) {
    while (1) {
        XhciTRB* event = &controller->event_ring[controller->event_ring_dequeue];
        
        // Check if this event has the correct cycle bit
        uint32_t cycle = (event->control & TRB_CYCLE_BIT) ? 1 : 0;
        if (cycle != controller->event_ring_cycle) {
            break; // No more events
        }
        
        // Process the event based on its type
        uint32_t trb_type = (event->control >> 10) & 0x3F;
        
        PrintKernel("xHCI: Event TRB Type: "); PrintKernelInt(trb_type); PrintKernel("\n");

        // Advance dequeue pointer
        controller->event_ring_dequeue = (controller->event_ring_dequeue + 1) % EVENT_RING_SIZE;
        if (controller->event_ring_dequeue == 0) {
            controller->event_ring_cycle = !controller->event_ring_cycle;
        }
        
        // Update event ring dequeue pointer in hardware
        volatile uint32_t* interrupter = &controller->runtime_regs[XHCI_RT_IR0 / 4];
        uint64_t erdp = VIRT_TO_PHYS((uint64_t)&controller->event_ring[controller->event_ring_dequeue]);
        xHCIWriteRegister64((volatile uint64_t*)&interrupter[4], erdp | (1 << 3)); // Set EHB bit
    }
}

// Enable a device slot
int xHCIEnableSlot(XhciController* controller) {
    PrintKernel("xHCI: Enabling device slot...\n");
    
    XhciTRB enable_slot_trb = {0};
    enable_slot_trb.control = (9 << 10); // Enable Slot Command TRB type
    
    xHCISubmitCommand(controller, &enable_slot_trb);
    
    // Wait for command completion event
    delay(10000); // 10ms delay
    xHCIProcessEvents(controller);
    
    return 1; // Return slot ID (simplified - should parse from command completion event)
}

// Address a device
int xHCIAddressDevice(XhciController* controller, uint8_t slot_id) {
    PrintKernel("xHCI: Addressing device in slot "); PrintKernelInt(slot_id); PrintKernel("\n");
    
    // Allocate device context for this slot
    XhciDeviceContext* dev_ctx = VMemAlloc(sizeof(XhciDeviceContext));
    if (!dev_ctx) {
        PrintKernelError("xHCI: Failed to allocate device context\n");
        return -1;
    }
    
    // Clear device context
    for (int i = 0; i < sizeof(XhciDeviceContext) / 4; i++) {
        ((uint32_t*)dev_ctx)[i] = 0;
    }
    
    // Set up slot context
    dev_ctx->slot.context_entries = 1; // Only EP0 for now
    dev_ctx->slot.root_hub_port_number = 1; // Assume port 1 for now
    dev_ctx->slot.route_string = 0;
    dev_ctx->slot.speed = 4; // Full speed (will be updated based on port status)
    
    // Set up EP0 context
    dev_ctx->endpoints[0].ep_type = 4; // Control endpoint
    dev_ctx->endpoints[0].max_packet_size = 64; // Default for full speed
    dev_ctx->endpoints[0].error_count = 3;
    dev_ctx->endpoints[0].tr_dequeue_pointer = 0; // Will be set when transfer ring is created
    
    // Store device context in DCBAA
    uint64_t dev_ctx_phys = VIRT_TO_PHYS((uint64_t)dev_ctx);
    controller->dcbaa[slot_id] = dev_ctx_phys;
    
    XhciTRB address_dev_trb = {0};
    address_dev_trb.parameter_lo = (uint32_t)(dev_ctx_phys & 0xFFFFFFFF);
    address_dev_trb.parameter_hi = (uint32_t)(dev_ctx_phys >> 32);
    address_dev_trb.control = (11 << 10) | (slot_id << 24); // Address Device Command TRB
    
    xHCISubmitCommand(controller, &address_dev_trb);
    
    // Wait for command completion
    delay(10000);
    xHCIProcessEvents(controller);
    
    PrintKernel("xHCI: Device addressed\n");
    return 0;
}

// Control transfer function
int xHCIControlTransfer(XhciController* controller, uint8_t slot_id, 
                         USBSetupPacket* setup, void* data, uint16_t length) {
    PrintKernel("xHCI: Performing control transfer for slot "); PrintKernelInt(slot_id); PrintKernel("\n");
    
    // Create transfer ring for this endpoint if not exists
    XhciTRB* transfer_ring = VMemAlloc(TRANSFER_RING_SIZE * sizeof(XhciTRB));
    if (!transfer_ring) {
        PrintKernelError("xHCI: Failed to allocate transfer ring\n");
        return -1;
    }
    
    // Clear transfer ring
    for (int i = 0; i < TRANSFER_RING_SIZE; i++) {
        transfer_ring[i].parameter_lo = 0;
        transfer_ring[i].parameter_hi = 0;
        transfer_ring[i].status = 0;
        transfer_ring[i].control = 0;
    }
    
    // Set up link TRB at end
    XhciTRB* link_trb = &transfer_ring[TRANSFER_RING_SIZE - 1];
    uint64_t ring_phys = VIRT_TO_PHYS((uint64_t)transfer_ring);
    link_trb->parameter_lo = (uint32_t)(ring_phys & 0xFFFFFFFF);
    link_trb->parameter_hi = (uint32_t)(ring_phys >> 32);
    link_trb->control = (TRB_TYPE_LINK << 10) | TRB_CYCLE_BIT;
    
    int trb_index = 0;
    
    // Setup stage TRB
    XhciTRB* setup_trb = &transfer_ring[trb_index++];
    setup_trb->parameter_lo = *((uint32_t*)setup);
    setup_trb->parameter_hi = *((uint32_t*)((uint8_t*)setup + 4));
    setup_trb->status = 8; // 8 bytes in setup packet
    setup_trb->control = (TRB_TYPE_SETUP << 10) | TRB_CYCLE_BIT | TRB_IDT;
    
    // Data stage TRB (if data present)
    if (data && length > 0) {
        XhciTRB* data_trb = &transfer_ring[trb_index++];
        uint64_t data_phys = VIRT_TO_PHYS((uint64_t)data);
        data_trb->parameter_lo = (uint32_t)(data_phys & 0xFFFFFFFF);
        data_trb->parameter_hi = (uint32_t)(data_phys >> 32);
        data_trb->status = length;
        data_trb->control = (TRB_TYPE_DATA << 10) | TRB_CYCLE_BIT;
        if (setup->bmRequestType & 0x80) {
            data_trb->control |= (1 << 16); // DIR bit for IN transfers
        }
    }
    
    // Status stage TRB
    XhciTRB* status_trb = &transfer_ring[trb_index++];
    status_trb->parameter_lo = 0;
    status_trb->parameter_hi = 0;
    status_trb->status = 0;
    status_trb->control = (TRB_TYPE_STATUS << 10) | TRB_CYCLE_BIT | TRB_IOC;
    if (!(setup->bmRequestType & 0x80) || length == 0) {
        status_trb->control |= (1 << 16); // DIR bit
    }
    
    // Ring doorbell for endpoint
    volatile uint32_t* doorbell_regs = (volatile uint32_t*)((uint8_t*)controller->operational_regs + 0x400 + (controller->max_ports * 0x10));
    xHCIWriteRegister(&doorbell_regs[slot_id], 1); // EP0
    
    // Wait for completion
    delay(50000); // 50ms
    xHCIProcessEvents(controller);
    
    // Cleanup
    VMemFree(transfer_ring, TRANSFER_RING_SIZE * sizeof(XhciTRB));
    
    PrintKernel("xHCI: Control transfer completed\n");
    return 0;
}

// USB device enumeration
static int xHCIEnumerateDevice(XhciController* controller, uint8_t port) {
    PrintKernel("xHCI: Enumerating device on port "); PrintKernelInt(port + 1); PrintKernel("\n");
    
    // Enable a slot for this device
    uint8_t slot_id = xHCIEnableSlot(controller);
    if (slot_id == 0) {
        PrintKernelError("xHCI: Failed to enable slot\n");
        return -1;
    }
    
    // Address the device
    if (xHCIAddressDevice(controller, slot_id) != 0) {
        PrintKernelError("xHCI: Failed to address device\n");
        return -1;
    }

    // Get device descriptor
    USBSetupPacket get_device_desc = {
        .bmRequestType = USB_REQTYPE_DIR_IN | USB_REQTYPE_TYPE_STD | USB_REQTYPE_RECIP_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (USB_DESC_DEVICE << 8) | 0,
        .wIndex = 0,
        .wLength = 18
    };



    USBDeviceDescriptor* device_desc = VMemAlloc(sizeof(USBDeviceDescriptor));
    if (!device_desc) {
        PrintKernelError("xHCI: Failed to allocate device descriptor buffer\n");
        return -1;
    }
    
    if (xHCIControlTransfer(controller, slot_id, &get_device_desc, device_desc, 18) == 0) {
        PrintKernel("xHCI: Device enumerated successfully!\n");
        PrintKernel("  Vendor ID: 0x"); PrintKernelHex(device_desc->idVendor); PrintKernel("\n");
        PrintKernel("  Product ID: 0x"); PrintKernelHex(device_desc->idProduct); PrintKernel("\n");
        PrintKernel("  Device Class: 0x"); PrintKernelHex(device_desc->bDeviceClass); PrintKernel("\n");
    }

    if (device_desc->bDeviceClass == 0x03) { // HID class
        PrintKernelSuccess("xHCI: HID keyboard detected!\n");
        xHCISetupUSBKeyboard(controller, slot_id);
    }

    VMemFree(device_desc, sizeof(USBDeviceDescriptor));
    return 0;
}

// Enhanced port scanning with enumeration
void xHCIScanAndEnumeratePorts(XhciController* controller) {
    PrintKernel("xHCI: Scanning and enumerating ports...\n");
    
    volatile uint32_t* port_regs = (volatile uint32_t*)((uint8_t*)controller->operational_regs + 0x400);
    
    for (uint32_t port = 0; port < controller->max_ports; port++) {
        volatile uint32_t* port_sc = &port_regs[port * 4];
        uint32_t status = xHCIReadRegister(port_sc);
        
        PrintKernel("xHCI: Port "); PrintKernelInt(port + 1); PrintKernel(": ");
        
        if (status & PORTSC_CCS) {
            PrintKernel("Device connected");
            uint32_t speed = (status & PORTSC_SPEED_MASK) >> 10;
            PrintKernel(" (Speed: "); PrintKernelInt(speed); PrintKernel(")");
            
            // Power on port if not already powered
            if (!(status & PORTSC_PP)) {
                PrintKernel(" - Powering on port");
                xHCIWriteRegister(port_sc, status | PORTSC_PP);
                delay(20000); // Wait 20ms for power stabilization
            }
            
            // Reset port to enable it
            if (!(status & PORTSC_PED)) {
                PrintKernel(" - Resetting port");
                xHCIWriteRegister(port_sc, status | PORTSC_PR);
                
                // Wait for reset to complete
                int timeout = 100;
                while (timeout > 0) {
                    status = xHCIReadRegister(port_sc);
                    if (!(status & PORTSC_PR)) break;
                    delay(1000);
                    timeout--;
                }
                
                if (status & PORTSC_PED) {
                    PrintKernel(" - Port enabled");
                    PrintKernel("\n");
                    
                    // Give device time to stabilize after reset
                    delay(100000); // 100ms
                    
                    // Enumerate the device
                    xHCIEnumerateDevice(controller, port);
                } else {
                    PrintKernel(" - Port enable failed");
                }
            } else {
                PrintKernel(" - Already enabled");
                PrintKernel("\n");
                xHCIEnumerateDevice(controller, port);
            }
        } else {
            PrintKernel("No device");
        }
        PrintKernel("\n");
    }
}

// USB device scanner - like lsusb
void xHCIEnumerate(void) {
    PrintKernel("--- xHCI Enumeration ---\n");
    PciDevice xhci_pci_dev;
    if (PciFindByClass(0x0C, 0x03, 0x30, &xhci_pci_dev) == 0) {
        PrintKernel("Found xHCI Controller at PCI "); PrintKernelHex(xhci_pci_dev.bus); PrintKernel(":");
        PrintKernelHex(xhci_pci_dev.device); PrintKernel(":");
        PrintKernelHex(xhci_pci_dev.function); PrintKernel("\n");

        XhciController controller;
        if (xHCIControllerInit(&controller, &xhci_pci_dev) == 0) {

            PrintKernel("Scanning USB ports...\n");

            // Scan all ports
            for (uint32_t port = 1; port <= controller.max_ports; port++) {
                volatile uint32_t* port_regs = (volatile uint32_t*)
                    ((uint8_t*)controller.operational_regs + 0x400 + ((port - 1) * 0x10));

                uint32_t portsc = *port_regs;

                if (portsc & 0x1) { // CCS - Device connected
                    PrintKernel("Port "); PrintKernelInt(port); PrintKernel(": Device connected\n");

                    // Try to get device info
                    int slot_id = xHCIEnableSlot(&controller);
                    if (slot_id > 0) {
                        if (xHCIAddressDevice(&controller, slot_id) == 0) {

                            // Get device descriptor
                            USBSetupPacket setup = {
                                .bmRequestType = 0x80,
                                .bRequest = 6,
                                .wValue = 0x0100,
                                .wIndex = 0,
                                .wLength = sizeof(USBDeviceDescriptor)
                            };

                            USBDeviceDescriptor device_desc;
                            if (xHCIControlTransfer(&controller, slot_id, &setup,
                                                    &device_desc, sizeof(device_desc)) == 0) {

                                PrintKernel("  Vendor: "); PrintKernelHex(device_desc.idVendor);
                                PrintKernel("  Product: "); PrintKernelHex(device_desc.idProduct);
                                PrintKernel("  Class: "); PrintKernelHex(device_desc.bDeviceClass);
                                PrintKernel("\n");

                                // Device type
                                switch (device_desc.bDeviceClass) {
                                    case 0x03: PrintKernel("  Type: HID Device (Keyboard/Mouse)\n"); break;
                                    case 0x08: PrintKernel("  Type: Mass Storage Device\n"); break;
                                    case 0x09: PrintKernel("  Type: USB Hub\n"); break;
                                    default: PrintKernel("  Type: Other Device\n"); break;
                                }
                            } else {
                                PrintKernel("  Failed to read device descriptor\n");
                            }
                        } else {
                            PrintKernel("  Failed to address device\n");
                        }
                    } else {
                        PrintKernel("  Failed to enable slot\n");
                    }
                } else {
                    PrintKernel("Port "); PrintKernelInt(port); PrintKernel(": No device\n");
                }
            }

            xHCIControllerCleanup(&controller);
        } else {
            PrintKernelError("Failed to initialize xHCI controller\n");
        }
    } else {
        PrintKernel("No xHCI controller found\n");
    }

    PrintKernel("------------------\n");
}

void xHCIInit() {
    PciDevice xhci_pci_dev;
    if (PciFindByClass(0x0C, 0x03, 0x30, &xhci_pci_dev) == 0) {
        PrintKernelSuccess("xHCI: Found an xHCI controller!\n");
        XhciController controller;
        if (xHCIControllerInit(&controller, &xhci_pci_dev) == 0) {
            PrintKernelSuccess("xHCI: xHCI driver initialization succeeded!\n");
            
            // Enumerate connected devices
            xHCIScanAndEnumeratePorts(&controller);
            
            // Store the controller globally or pass it to a USB stack
        } else {
            PrintKernelError("xHCI: xHCI driver initialization failed!\n");
        }

    } else {
        PrintKernel("xHCI: No xHCI controller found on the system.\n");
    }
}

int xHCIConfigureEndpoint(XhciController* controller, uint8_t slot_id) {
    if (!controller || slot_id == 0 || slot_id > controller->max_slots) {
        return -1;
    }

    XhciDeviceContext* dev_ctx = controller->device_contexts[slot_id - 1];
    if (!dev_ctx) {
        return -1;
    }

    // Configure endpoint 1 IN (interrupt endpoint for keyboard)
    XhciEndpointContext* ep_ctx = &dev_ctx->endpoints[1];

    ep_ctx->ep_type = 3;           // Interrupt IN
    ep_ctx->max_packet_size = 8;   // Standard HID keyboard report
    ep_ctx->interval = 3;          // 8ms polling
    ep_ctx->max_burst_size = 0;
    ep_ctx->error_count = 3;

    // Allocate transfer ring for endpoint
    XhciTRB* ep_ring = (XhciTRB*)VMemAlloc(1);
    if (!ep_ring) return -1;

    FastMemset(ep_ring, 0, 4096);

    // Setup link TRB for circular ring
    ep_ring[255].parameter_lo = (uint32_t)(uintptr_t)ep_ring;
    ep_ring[255].parameter_hi = (uint32_t)((uintptr_t)ep_ring >> 32);
    ep_ring[255].control = (6 << 10) | (1 << 1) | 1;

    ep_ctx->tr_dequeue_pointer = (uint64_t)(uintptr_t)ep_ring | 1;

    // Submit Configure Endpoint command
    uint32_t cmd_idx = controller->command_ring_enqueue;
    XhciTRB* cmd_trb = &controller->command_ring[cmd_idx];

    cmd_trb->parameter_lo = (uint32_t)(uintptr_t)dev_ctx;
    cmd_trb->parameter_hi = (uint32_t)((uintptr_t)dev_ctx >> 32);
    cmd_trb->status = 0;
    cmd_trb->control = (11 << 10) | (controller->command_ring_cycle ? 1 : 0);

    controller->command_ring_enqueue = (controller->command_ring_enqueue + 1) % 256;
    if (controller->command_ring_enqueue == 0) {
        controller->command_ring_cycle = !controller->command_ring_cycle;
    }

    xHCIRingCommandDoorbell(controller);
    delay(10000);
    xHCIProcessEvents(controller);

    return 0;
}

int xHCIInterruptTransfer(XhciController* controller, uint8_t slot_id,
                           uint8_t endpoint, void* buffer, uint16_t length) {
    if (!controller || !buffer || slot_id == 0) return -1;

    XhciDeviceContext* dev_ctx = controller->device_contexts[slot_id - 1];
    if (!dev_ctx) return -1;

    XhciEndpointContext* ep_ctx = &dev_ctx->endpoints[endpoint];
    XhciTRB* ep_ring = (XhciTRB*)(uintptr_t)(ep_ctx->tr_dequeue_pointer & ~0xFULL);

    if (!ep_ring) return -1;

    static uint32_t ep_enqueue = 0;
    XhciTRB* data_trb = &ep_ring[ep_enqueue];

    data_trb->parameter_lo = (uint32_t)(uintptr_t)buffer;
    data_trb->parameter_hi = (uint32_t)((uintptr_t)buffer >> 32);
    data_trb->status = length;
    data_trb->control = (1 << 10) | (1 << 5) | 1; // Normal TRB, IOC, Cycle

    ep_enqueue = (ep_enqueue + 1) % 255;

    // Ring doorbell for endpoint
    volatile uint32_t* doorbell = (volatile uint32_t*)((uintptr_t)controller->mmio_base +
                                                       0x1000 + (slot_id * 4));
    *doorbell = endpoint;

    return 0;
}

// USB Keyboard setup function
void xHCISetupUSBKeyboard(XhciController* controller, uint8_t slot_id) {
    PrintKernelSuccess("xHCI: Configuring USB keyboard on slot ");
    PrintKernelInt(slot_id);
    PrintKernel("\n");

    if (xHCIConfigureEndpoint(controller, slot_id) == 0) {
        PrintKernelSuccess("xHCI: USB keyboard configured and ready!\n");

        // Setup continuous interrupt transfers for keyboard input
        static USBHIDKeyboardReport kbd_report;
        xHCIInterruptTransfer(controller, slot_id, 1, &kbd_report, sizeof(kbd_report));
    } else {
        PrintKernelError("xHCI: Failed to configure keyboard endpoint\n");
    }
}