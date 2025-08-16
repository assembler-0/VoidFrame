#include "LPT.h"
#include "Io.h"
#include "Console.h"
#include "ISA.h"
#include "stdint.h"

#define LPT_DATA_PORT    0
#define LPT_STATUS_PORT  1
#define LPT_CONTROL_PORT 2

#define LPT_STATUS_NOT_BUSY 0x80  // Bit 7: 1 = not busy, 0 = busy (inverted)
#define LPT_CONTROL_STROBE 0x01

static uint16_t g_lpt_io_base = 0;

void LPT_Init() {
    // Try ISA detection first
    IsaDevice* lpt_dev = IsaFindDeviceByType(ISA_DEVICE_PARALLEL);

    if (lpt_dev != NULL) {
        g_lpt_io_base = lpt_dev->io_base;
        PrintKernelF("LPT Driver: Found LPT1 via ISA at I/O base 0x%X\n", g_lpt_io_base);
    } else {
        // Fallback: QEMU should emulate LPT1 at standard address
        g_lpt_io_base = 0x378;
        PrintKernelF("LPT Driver: ISA detection failed, forcing standard LPT1 address 0x378\n");

        // Test if the port responds
        uint8_t status = inb(g_lpt_io_base + LPT_STATUS_PORT);
        PrintKernelF("LPT Driver: Status register reads 0x%02X\n", status);
    }
}

void LPT_WriteChar(char c) {
    if (g_lpt_io_base == 0) return;

    // FIXED: Wait while busy (bit 7 clear = busy, bit 7 set = not busy)
    int timeout = 100000;
    while (((inb(g_lpt_io_base + LPT_STATUS_PORT) & LPT_STATUS_NOT_BUSY) == 0) && timeout-- > 0) {
        // Wait while port is busy
    }

    if (timeout <= 0) {
        PrintKernelError("LPT: Timeout waiting for port ready\n");
        return;
    }

    // Write data with proper timing
    outb(g_lpt_io_base + LPT_DATA_PORT, c);

    // Data setup time (at least 0.5μs)
    for(volatile int i = 0; i < 10; i++);

    // FIXED: Proper strobe pulse with timing
    uint8_t control_val = inb(g_lpt_io_base + LPT_CONTROL_PORT);

    // Assert strobe (bit 1 -> pin LOW due to inversion)
    outb(g_lpt_io_base + LPT_CONTROL_PORT, control_val | LPT_CONTROL_STROBE);

    // Strobe pulse width (minimum 0.5 μs, using ~10μs for safety)
    for(volatile int i = 0; i < 50; i++);

    // Release strobe (bit 0 -> pin HIGH)
    outb(g_lpt_io_base + LPT_CONTROL_PORT, control_val & ~LPT_CONTROL_STROBE);

    // Data hold time
    for(volatile int i = 0; i < 10; i++);
}

void LPT_WriteString(const char* str) {
    if (g_lpt_io_base == 0) return;

    for (int i = 0; str[i] != '\0'; i++) {
        LPT_WriteChar(str[i]);
    }
}