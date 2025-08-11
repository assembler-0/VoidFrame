#ifndef PCI_H
#define PCI_H

#include "stdint.h" // Or your equivalent for standard types

// A structure to hold information about a discovered PCI device
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if; // Programming Interface
} PciDevice;

static PciDevice found_device;
static int device_found_flag;
static uint16_t target_vendor_id;
static uint16_t target_device_id;
// Callback function pointer type
typedef void (*PciDeviceCallback)(PciDevice device);

// Function prototypes
void PciEnumerate();
int PciFindDevice(uint16_t vendor_id, uint16_t device_id, PciDevice* out_device);
uint32_t PciConfigReadDWord(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

#endif // PCI_H