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
    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t max_intrs;
} XhciController;

typedef struct {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
} XhciTRB; // transfer req block

typedef struct {
    uint64_t address;
    uint32_t size;
    uint32_t reserved;
} XhciERSTEntry; // Event Ring Segment

void xHCIInit();
void xHCIControllerCleanup(XhciController* controller);

#endif // XHCI_H