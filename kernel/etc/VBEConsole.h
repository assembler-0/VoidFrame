// drivers/VBEConsole.h
#ifndef VBE_CONSOLE_H
#define VBE_CONSOLE_H

#include "stdint.h"

#define VBE_CONSOLE_WIDTH  100
#define VBE_CONSOLE_HEIGHT 75

void VBEConsoleInit(void);
void VBEConsoleClear(void);
void VBEConsolePutChar(char c);
void VBEConsolePrint(const char* str);
void VBEConsoleSetColor(uint8_t color);
void VBEConsoleSetCursor(uint32_t x, uint32_t y);
void VBEConsoleRefresh(void);

#endif // VBE_CONSOLE_H