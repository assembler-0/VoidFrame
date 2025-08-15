#ifndef VOIDFRAME_SB16_H
#define VOIDFRAME_SB16_H

#include "stdint.h"

#define SB16_DSP_BASE       0x220
#define SB16_MIXER_BASE     0x224
#define SB16_OPL3_BASE      0x388
#define SB16_MPU401_BASE    0x330

int SB16_Probe(uint16_t io_base);

#endif // VOIDFRAME_SB16_H
