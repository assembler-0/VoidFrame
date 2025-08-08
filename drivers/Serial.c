#include "Serial.h"
#include "Io.h"

#define COM1 0x3f8   // COM1

void SerialInit(void) {
    outb(COM1 + 1, 0x00);    // Disable all interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) -> 38400 baud
    outb(COM1 + 1, 0x00);    //                  (hi byte)
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1 + 2, 0x01);    // Enable FIFO with 1-byte threshold
    outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}
int is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void SerialWriteChar(const char a) {
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, a);

    // If it's a newline, also send carriage return
    if (a == '\n') {
        while ((inb(COM1 + 5) & 0x20) == 0) {}
        outb(COM1, '\r');
    }
}

void SerialWrite(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        SerialWriteChar(str[i]);
    }
}