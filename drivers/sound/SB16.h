#ifndef VOIDFRAME_SB16_H
#define VOIDFRAME_SB16_H

#include "Io.h"
#include "stdint.h"

#define SB16_DSP_BASE       0x220
#define SB16_MIXER_BASE     0x224
#define SB16_OPL3_BASE      0x388
#define SB16_MPU401_BASE    0x330

#define SB16_DSP_RESET      0x6
#define SB16_DSP_READ       0xA
#define SB16_DSP_WRITE      0xC
#define SB16_DSP_STATUS     0xC
#define SB16_DSP_READ_STATUS 0xE

int SB16_Probe(uint16_t io_base);
void SB16_Beep(uint16_t io_base);

static inline void dsp_write(uint16_t io_base, uint8_t value) {
    // Wait until the DSP is not busy (bit 7 of the status port is 0)
    while (inb(io_base + SB16_DSP_STATUS) & 0x80) {}

    // Send the command/data byte
    outb(io_base + SB16_DSP_WRITE, value);
}

#endif // VOIDFRAME_SB16_H
