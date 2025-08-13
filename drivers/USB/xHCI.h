#ifndef XHCI_H
#define XHCI_H

#include "PCI/PCI.h" // Assuming this is your PCI header
#include "stdint.h"

// We will add more to this structure later
typedef struct {
    PciDevice pci_device;
    // Volatile is crucial here! It tells the compiler that this memory
    // can be changed by something else (the hardware itself).
    volatile uint8_t* mmio_base;
    // We'll also store pointers to specific register groups
    volatile uint32_t* operational_regs;
    volatile uint32_t* runtime_regs;

} XhciController;


// The main initialization function for our driver
void xhci_init();

#endif // XHCI_H