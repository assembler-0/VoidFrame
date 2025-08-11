#include "PCI/PCI.h"

#include "Console.h"
#include "Io.h"
#include "stdint.h"

static PciDevice found_device;
static int device_found_flag;
static uint16_t target_vendor_id;
static uint16_t target_device_id;
// Callback function pointer type
typedef void (*PciDeviceCallback)(PciDevice device);

uint32_t PciConfigReadDWord(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    outl(0xCF8, address);
    return inl(0xCFC);
}

uint8_t PciConfigReadByte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return PciConfigReadDWord(bus, slot, func, offset) & 0xFF;
}

// The core scanning logic, now separated and reusable
static void PciScanBus(PciDeviceCallback callback) {
    for (int bus = 0; bus < 256; bus++) {
        for (int device = 0; device < 32; device++) {
            // Read header type to check for multi-function devices
            uint32_t header_type_reg = PciConfigReadDWord(bus, device, 0, 0x0C);
            uint8_t header_type = (header_type_reg >> 16) & 0xFF;
            int max_funcs = (header_type & 0x80) ? 8 : 1; // Is multi-function bit set?

            for (int func = 0; func < max_funcs; func++) {
                uint32_t id_reg = PciConfigReadDWord(bus, device, func, 0x00);
                uint16_t vendor_id = id_reg & 0xFFFF;

                if (vendor_id == 0xFFFF) continue; // Device doesn't exist

                PciDevice pci_dev;
                pci_dev.bus = bus;
                pci_dev.device = device;
                pci_dev.function = func;
                pci_dev.vendor_id = vendor_id;
                pci_dev.device_id = id_reg >> 16;

                uint32_t class_reg = PciConfigReadDWord(bus, device, func, 0x08);
                pci_dev.class_code = (class_reg >> 24) & 0xFF;
                pci_dev.subclass = (class_reg >> 16) & 0xFF;
                pci_dev.prog_if = (class_reg >> 8) & 0xFF;

                // Call the provided callback with the device info
                callback(pci_dev);
            }
        }
    }
}

// Your old PciEnumerate function, now a simple wrapper around the scanner
static void PrintPciDeviceInfo(PciDevice device) {
    PrintKernel("PCI: B:0x"); PrintKernelHex(device.bus);
    PrintKernel(" D:0x"); PrintKernelHex(device.device);
    PrintKernel(" F:0x"); PrintKernelHex(device.function);
    PrintKernel(" -> VID:0x"); PrintKernelHex(device.vendor_id);
    PrintKernel(" DID:0x"); PrintKernelHex(device.device_id);
    PrintKernel(" (C:0x"); PrintKernelHex(device.class_code);
    PrintKernel(" S:0x"); PrintKernelHex(device.subclass);
    PrintKernel(")\n");
}

void PciEnumerate() {
    PrintKernel("--- PCI Bus Enumeration ---\n");
    PciScanBus(PrintPciDeviceInfo);
    PrintKernel("---------------------------\n");
}

static void FindDeviceCallback(PciDevice device) {
    if (device_found_flag) return; // We already found it, stop searching

    if (device.vendor_id == target_vendor_id && device.device_id == target_device_id) {
        found_device = device;
        device_found_flag = 1;
    }
}

// The public helper function to find a device
int PciFindDevice(uint16_t vendor_id, uint16_t device_id, PciDevice* out_device) {
    // Set up the search criteria
    target_vendor_id = vendor_id;
    target_device_id = device_id;
    device_found_flag = 0;

    // Scan the bus using our specific search callback
    PciScanBus(FindDeviceCallback);

    if (device_found_flag) {
        *out_device = found_device;
        return 0; // Success
    }

    return -1; // Failure
}