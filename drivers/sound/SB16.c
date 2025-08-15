#include "SB16.h"
#include "Io.h"
#include "VesaBIOSExtension.h"
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
    delay(10000);

    dsp_write(io_base, 0xD1);  // Turn speaker on
    delay(1000);               // Small delay after speaker enable

    // Set the sample rate
    dsp_write(io_base, 0x40);
    dsp_write(io_base, 0xA8);

    // Tell the DSP we are sending a block of data
    dsp_write(io_base, 0x14);

    // Send the length
    uint16_t length = 256;
    dsp_write(io_base, (length - 1) & 0xFF);
    dsp_write(io_base, ((length - 1) >> 8) & 0xFF);

    // Send the actual audio data
    for (int j = 0; j < length; j++) {
        uint8_t sample = (j % 16 < 8) ? 0xF0 : 0x10;
        dsp_write(io_base, sample);
    }
}