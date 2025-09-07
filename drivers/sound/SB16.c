#include "SB16.h"
#include "Io.h"
#include "Cpu.h"
#include "stdint.h"

int SB16_Probe(uint16_t io_base) {
    // 1. Pulse the reset line high.
    outb(io_base + SB16_DSP_RESET, 1);

    // 2. Wait for at least 3 microseconds. A small loop is sufficient here.
    delay(1000); // Adjust loop count if needed.

    // 3. Release the reset line by bringing it low. THIS IS THE CRITICAL FIX.
    outb(io_base + SB16_DSP_RESET, 0);

    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(io_base + SB16_DSP_READ_STATUS) & 0x80) {
            // Data is ready in the buffer.

            // 5. Read the identification value from the data port (0xA).
            uint8_t data = inb(io_base + SB16_DSP_READ);

            // 6. Check if we got the magic value.
            if (data == 0xAA) {
                return 1; // Success! Device found.
            }
            return 0; // Got a response, but it was incorrect.
        }
    }

    return 0; // Timeout: Device did not respond after reset.
}

void SB16_Beep(uint16_t io_base) {
    // Turn speaker on
    dsp_write(io_base, 0xD1);
    
    // Set sample rate
    dsp_write(io_base, 0x40);
    dsp_write(io_base, 0xA6);
    
    // Use direct mode (0x10) instead of DMA
    for (int i = 0; i < 8000; i++) {
        dsp_write(io_base, 0x10);  // Direct 8-bit output
        dsp_write(io_base, (i % 64 < 32) ? 0xFF : 0x00);  // Square wave
        delay(10);
    }
    
    // Turn speaker off
    dsp_write(io_base, 0xD3);
}