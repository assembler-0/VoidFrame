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

    // --- Step 1: Set the sample rate ---
    // Command 0x40: Set Time Constant
    // Sample Rate = 1,000,000 / (256 - time_constant)
    // We will aim for 8000 Hz. time_constant = 256 - (1,000,000 / 8000) = 131
    dsp_write(io_base, 0x40);
    dsp_write(io_base, 131); // Time constant for ~8000 Hz

    // --- Step 2: Tell the DSP we are sending a block of data ---
    // Command 0x14: Single-cycle (PIO) 8-bit Output
    dsp_write(io_base, 0x14);

    // Send the length of the data block (e.g., 256 bytes).
    // The length is sent as (length - 1), low-byte first.
    // For 256 bytes, length-1 is 255.
    uint16_t length = 256;
    dsp_write(io_base, (length - 1) & 0xFF);       // Low byte of length
    dsp_write(io_base, ((length - 1) >> 8) & 0xFF); // High byte of length

    // --- Step 3: Send the actual audio data (our square wave) ---
    for (int j = 0; j < length; j++) {
        // Alternate between a high and low value to create the wave
        // Every 8 samples we flip the value. This determines the pitch.
        if (j % 16 < 8) {
            dsp_write(io_base, 0xF0); // High value
        } else {
            dsp_write(io_base, 0x10); // Low value
        }
    }
}