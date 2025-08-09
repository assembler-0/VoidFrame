#ifndef VOIDFRAME_SERIAL_H
#define VOIDFRAME_SERIAL_H

#include <stdint.h>

#define COM1 0x3f8   // COM1
#define COM2 0x2f8   // COM2
#define COM3 0x3e8   // COM3
#define COM4 0x2e8   // COM4

// Serial port addresses
#define SERIAL_COM1 0x3F8
#define SERIAL_COM2 0x2F8
#define SERIAL_COM3 0x3E8
#define SERIAL_COM4 0x2E8

// Initialize serial port (defaults to COM1)
int SerialInit(void);

// Initialize specific serial port
int SerialInitPort(uint16_t port);

// Status functions
int SerialTransmitEmpty(void);
int SerialDataAvailable(void);

// Character I/O
int SerialWriteChar(const char c);
int SerialReadChar(void);

// String I/O
int SerialWrite(const char* str);
int SerialReadLine(char* buffer, int max_length);

// Number output
void SerialWriteHex(uint64_t value);
void SerialWriteDec(uint64_t value);

#endif // VOIDFRAME_SERIAL_H