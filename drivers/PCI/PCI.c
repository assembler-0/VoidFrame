#include "PCI/PCI.h"

#include "Console.h"
#include "Io.h"
#include "VesaBIOSExtension.h"
#include "stdbool.h"
#include "stdint.h"

static uint8_t target_class;
static uint8_t target_subclass;
static uint8_t target_prog_if;

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

void PciConfigWriteDWord(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t data) {
    if (offset & 0x03) {
        PrintKernelWarning("PciConfigWriteDWord: Offset must be 4-byte aligned\n");
        return;
    }

    uint32_t address;
    uint32_t lbus  = (uint32_t)bus;
    uint32_t lslot = (uint32_t)slot;
    uint32_t lfunc = (uint32_t)func;

    address = (uint32_t)((lbus << 16) | (lslot << 11) |
              (lfunc << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));

    outl(0xCF8, address);
    outl(0xCFC, data);
}

void PciConfigWriteByte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t data) {
    // For byte writes, we need to read-modify-write the dword
    uint32_t dword_offset = offset & 0xFC;
    uint32_t byte_offset = offset & 0x03;

    uint32_t current_value = PciConfigReadDWord(bus, slot, func, dword_offset);
    uint32_t mask = ~(0xFF << (byte_offset * 8));
    uint32_t new_value = (current_value & mask) | ((uint32_t)data << (byte_offset * 8));

    PciConfigWriteDWord(bus, slot, func, dword_offset, new_value);
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

void FindDeviceCallback(PciDevice device) {
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

static void FindByClassCallback(PciDevice device) {
    if (device_found_flag) return;

    if (device.class_code == target_class &&
        device.subclass == target_subclass &&
        device.prog_if == target_prog_if) {
        found_device = device;
        device_found_flag = 1;
    }
}

int PciFindByClass(uint8_t class_code, uint8_t subclass, uint8_t prog_if, PciDevice* out_device) {
    target_class = class_code;
    target_subclass = subclass;
    target_prog_if = prog_if;
    device_found_flag = 0;
    delay(1000); // Temporary timing fix
    PciScanBus(FindByClassCallback);
    if (device_found_flag) {
        *out_device = found_device;
        return 0; // Success
    }
    return -1; // Failure
}

uint64_t GetPCIMMIOSize(const PciDevice* pci_dev, uint32_t bar_value) {
    PrintKernel("GetPCIMMIOSize: Calculating BAR size for device...\n");

    // Determine which BAR register this is (0x10, 0x14, 0x18, 0x1C, 0x20, 0x24)
    uint8_t bar_offset = 0x10; // We'll assume BAR0 for now, but this could be parameterized

    // Read the original BAR value
    uint32_t original_bar = bar_value;
    uint32_t actual_bar = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset);

    if (actual_bar != bar_value) {
        PrintKernelWarning("GetPCIMMIOSize: BAR value mismatch, using hardware value\n");
        original_bar = actual_bar;
    }
    // Check if this is a 64-bit BAR
    bool is_64bit = ((original_bar & 0x06) == 0x04);
    uint32_t original_bar_high = 0;

    if (is_64bit) {
        // Read the upper 32 bits for 64-bit BARs
        original_bar_high = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset + 4);
        PrintKernel("GetPCIMMIOSize: Detected 64-bit BAR\n");
    }

    // Write all 1s to the BAR to determine size
    PciConfigWriteDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset, 0xFFFFFFFF);
    if (is_64bit) {
        PciConfigWriteDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset + 4, 0xFFFFFFFF);
    }

    // Read back to see which bits are implemented
    uint32_t size_mask = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset);
    uint32_t size_mask_high = 0;

    if (is_64bit) {
        size_mask_high = PciConfigReadDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset + 4);
    }

    // Restore the original BAR values
    PciConfigWriteDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset, original_bar);
    if (is_64bit) {
        PciConfigWriteDWord(pci_dev->bus, pci_dev->device, pci_dev->function, bar_offset + 4, original_bar_high);
    }

    // Calculate size from the mask
    // Clear the lower 4 bits (they're not part of the address)
    size_mask &= 0xFFFFFFF0;

    if (size_mask == 0) {
        PrintKernel("GetPCIMMIOSize: BAR not implemented or error\n");
        return 0;
    }

    // Find the size by looking for the first set bit from the right
    // Size = ~mask + 1
    uint64_t full_mask = size_mask;
    if (is_64bit) {
        full_mask |= ((uint64_t)size_mask_high << 32);
    }

    uint64_t size = (~full_mask + 1) & full_mask;

    // For 32-bit calculations, ensure we handle it properly
    if (!is_64bit) {
        size = (~(uint32_t)size_mask + 1) & 0xFFFFFFFF;
    }

    PrintKernel("GetPCIMMIOSize: Calculated BAR size: 0x"); PrintKernelHex(size); PrintKernel("\n");

    // Sanity check - xHCI controllers typically have 8KB-64KB of registers
    if (size < 0x1000 || size > 0x100000) {
        PrintKernel("GetPCIMMIOSize: Warning - unusual BAR size, using 64KB default\n");
        return 0x10000; // 64KB default
    }

    return size;
}