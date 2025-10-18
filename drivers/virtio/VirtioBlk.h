#ifndef VOIDFRAME_VIRTIOBLK_H
#define VOIDFRAME_VIRTIOBLK_H

#include "PCI/PCI.h"
#include "BlockDevice.h"

void InitializeVirtioBlk(PciDevice device);
int VirtioBlkRead(uint64_t sector, void* buffer, uint32_t count);
int VirtioBlkWrite(uint64_t sector, void* buffer, uint32_t count);
#endif //VOIDFRAME_VIRTIOBLK_H
