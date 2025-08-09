#ifndef VOIDFRAME_SERIAL_H
#define VOIDFRAME_SERIAL_H
#include "stdint.h"

void SerialInit(void);
void SerialWriteChar(char c);
void SerialWrite(const char* str);
void SerialWriteHex(uint64_t value);

#endif // VOIDFRAME_SERIAL_H
