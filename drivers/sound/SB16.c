#include "SB16.h"
#include "Io.h"
#include "VesaBIOSExtension.h"
#include "stdint.h"

int SB16_Probe(uint16_t io_base) {
    // 1. Reset the DSP
    outb(io_base + 0x6, 1);

    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(io_base + 0xE) & 0x80) break;
    }
    if (timeout <= 0) {
        return 0; // No response
    }
    // 3. Read the DSP identification value
    uint8_t data = inb(io_base + 0xA);

    // 4. Return true if we got the magic value
    if (data == 0xAA) {
        return 1; // Device found
    }

    return 0; // Device not found
}
