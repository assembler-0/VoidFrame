#include "SB16.h"
#include "Io.h"
#include "VesaBIOSExtension.h"
#include "stdint.h"

int SB16_Probe(uint16_t io_base) {
    // 1. Reset the DSP
    outb(io_base + 0x6, 1);

    delay(100);

    outb(io_base + 0x6, 0);

    delay(100);

    uint8_t data = inb(io_base + 0xA);

    // 4. Return true if we got the magic value
    if (data == 0xAA) {
        return 1; // Device found
    }

    return 0; // Device not found
}
