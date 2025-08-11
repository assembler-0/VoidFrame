#include "Pic.h"
#include "Io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_ICW4    0x01
#define ICW1_INIT    0x10
#define ICW4_8086    0x01

uint16_t PIT_FREQUENCY_HZ = 250;

void PitInstall() {
    const uint16_t divisor = 1193180 / PIT_FREQUENCY_HZ;

    outb(0x43, 0x36);  // Command byte: channel 0, lobyte/hibyte, rate generator
    outb(0x40, divisor & 0xFF);        // Low byte
    outb(0x40, (divisor >> 8) & 0xFF); // High byte
}


void PicInstall() {
    uint8_t a1, a2;

    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    outb(PIC1_DATA, 0x20); // Master PIC vector offset (0x20-0x27)
    outb(PIC2_DATA, 0x28); // Slave PIC vector offset (0x28-0x2F)

    outb(PIC1_DATA, 0x04); // Tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC2_DATA, 0x02); // Tell Slave PIC its cascade identity (0000 0010)

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // Mask all interrupts initially
    outb(PIC1_DATA, 0xFF); // Mask all on master PIC
    outb(PIC2_DATA, 0xFF); // Mask all on slave PIC

    // Restore original masks (a1, a2) - this will unmask IRQ2 on master
    // and whatever was unmasked on slave. Then explicitly unmask IRQ0.
    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);

    // Explicitly unmask IRQ0 (timer) on master PIC
    // Read current mask, clear bit 0, write back
    uint8_t pic1_mask = inb(PIC1_DATA);
    outb(PIC1_DATA, pic1_mask & ~0x01); // Clear bit 0 (IRQ0)

    // Initialize PIT after PIC
    PitInstall();
}
