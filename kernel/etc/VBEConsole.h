#ifndef VBE_CONSOLE_H
#define VBE_CONSOLE_H

#include "stdint.h"

// NEW: A clean define for the default console color (White on Black)
#define VBE_CONSOLE_DEFAULT_COLOR 0x07

void VBEConsoleInit(void);
void VBEConsoleClear(void);
void VBEConsoleRefresh(void);
void VBEConsolePutChar(char c);
void VBEConsolePrint(const char* str);
void VBEConsoleSetColor(uint8_t color);
void VBEConsoleSetCursor(uint32_t x, uint32_t y);

#endif // VBE_CONSOLE_H