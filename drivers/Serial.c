#include "Serial.h"
#include "Io.h"

#define COM1 0x3f8   // COM1

void SerialInit(void) {
    outb(COM1 + 1, 0x00);    // Disable all interrupts
    outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) -> 38400 baud
    outb(COM1 + 1, 0x00);    //                  (hi byte)
    outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
    // FIFO: enable (bit0), clear RX (bit1), clear TX (bit2), trigger=14 bytes (bits 6-7=11)
    outb(COM1 + 2, 0xC7);
    // Modem Control: DTR | RTS | OUT2 (keeps IRQ line enabled at PIC; actual serial IRQs still disabled since IER=0)
    outb(COM1 + 4, 0x0B);
}
int is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

void SerialWriteChar(const char a) {
    // If newline, emit CR first, then LF
    if (a == '\n') {
        while ((inb(COM1 + 5) & 0x20) == 0) {}
        outb(COM1, '\r');
    }
    while ((inb(COM1 + 5) & 0x20) == 0) {}
    outb(COM1, a);
}

void SerialWrite(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        SerialWriteChar(str[i]);
    }
}

void SerialWriteHex(uint64_t value) {
    const char hex[] = "0123456789ABCDEF";
    char buffer[17];
    buffer[16] = '\0';
    
    for (int i = 15; i >= 0; i--) {
        buffer[15-i] = hex[(value >> (i * 4)) & 0xF];
    }
    
    SerialWrite(buffer);
}