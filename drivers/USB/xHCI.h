#ifndef XHCI_H
#define XHCI_H

#include "PCI/PCI.h" // Assuming this is your PCI header
#include "stdint.h"

typedef struct {
    PciDevice pci_device;
    volatile uint8_t* mmio_base;
    volatile uint32_t* operational_regs;
    volatile uint32_t* runtime_regs;
    uint64_t mmio_size;
} XhciController;


void xHCIInit();
void xHCIControllerCleanup(XhciController* controller);

#endif // XHCI_H