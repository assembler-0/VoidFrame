#include "LPT.h"
#include "Io.h"
#include "Console.h"
#include "ISA.h"
#include "stdint.h"

#define LPT_DATA_PORT    0
#define LPT_STATUS_PORT  1
#define LPT_CONTROL_PORT 2

#define LPT_STATUS_BUSY 0x80
#define LPT_CONTROL_STROBE 0x01

static uint16_t g_lpt_io_base = 0;

void LPT_Init() {
    // Find the LPT device that the ISA bus detected
    IsaDevice* lpt_dev = IsaFindDeviceByType(ISA_DEVICE_PARALLEL);

    if (lpt_dev != NULL) {
        g_lpt_io_base = lpt_dev->io_base;
        PrintKernelF("LPT Driver: Found LPT1 at I/O base 0x%X\n", g_lpt_io_base);
    } else {
        PrintKernelError("LPT Driver: No parallel port device found by ISA bus.\n");
        g_lpt_io_base = 0; // Mark as not initialized
    }
}

void LPT_WriteChar(char c) {
    if (g_lpt_io_base == 0) return; // Don't do anything if not initialized

    int timeout = 100000;
    while ((inb(g_lpt_io_base + LPT_STATUS_PORT) & LPT_STATUS_BUSY) && timeout-- > 0) {
        // Do nothing, just wait
    }

    if (timeout <= 0) {
        // Could print an error here, the port is stuck
        return;
    }

    // 2. Write the character to the data port
    outb(g_lpt_io_base + LPT_DATA_PORT, c);

    // 3. Pulse the strobe bit to signal the printer to read the data
    uint8_t control_val = inb(g_lpt_io_base + LPT_CONTROL_PORT);
    // Set strobe high
    outb(g_lpt_io_base + LPT_CONTROL_PORT, control_val | LPT_CONTROL_STROBE);

    // A tiny delay
    for(volatile int i = 0; i < 5; i++);

    // Set strobe low again
    outb(g_lpt_io_base + LPT_CONTROL_PORT, control_val & ~LPT_CONTROL_STROBE);
}

void LPT_WriteString(const char* str) {
    if (g_lpt_io_base == 0) return;

    for (int i = 0; str[i] != '\0'; i++) {
        LPT_WriteChar(str[i]);
    }
}