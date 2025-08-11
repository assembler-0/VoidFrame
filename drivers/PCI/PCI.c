#include "PCI.h"

#include "Console.h"
#include "Io.h"
#include "stdint.h"

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

void PciEnumerate() {
    for (uint8_t bus = 0; bus < 255; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                const uint32_t data = PciConfigReadDWord(bus, device, func, 0x00);
                const uint16_t vendor_id = data & 0xFFFF;
                if (vendor_id == 0xFFFF) continue; // No device here
                const uint16_t device_id = (data >> 16) & 0xFFFF;
                PrintKernel("PCI Device: Bus "); PrintKernelHex(bus);
                PrintKernel(", Device "); PrintKernelHex(device);
                PrintKernel(", Func "); PrintKernelHex(func);
                PrintKernel(", Vendor 0x"); PrintKernelHex(vendor_id);
                PrintKernel(", Device 0x"); PrintKernelHex(device_id);
                PrintKernel("\n");
            }
        }
    }
}
