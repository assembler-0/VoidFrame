#ifndef VOIDFRAME_VIRTIOBLK_H
#define VOIDFRAME_VIRTIOBLK_H

#include "Virtio.h"
#include "PCI/PCI.h"

// VirtIO Block device specific definitions and function prototypes.

void InitializeVirtioBlk(PciDevice device);
int VirtioBlkRead(uint64_t sector, void* buffer);

#endif //VOIDFRAME_VIRTIOBLK_H
