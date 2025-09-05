#ifndef VOIDFRAME_VIRTIOBLK_H
#define VOIDFRAME_VIRTIOBLK_H

#include "PCI/PCI.h"

void InitializeVirtioBlk(PciDevice device);
int VirtioBlkRead(uint64_t sector, void* buffer);
int VirtioBlkWrite(uint64_t sector, void* buffer);
#endif //VOIDFRAME_VIRTIOBLK_H
