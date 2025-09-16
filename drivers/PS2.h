#include "stdint.h"

#ifndef PS2_H
#define PS2_H

// --- Event Handler Function Pointers ---
// These are weak symbols that can be overridden by the window manager.
void __attribute__((weak)) OnKeyPress(char c);
void __attribute__((weak)) OnMouseMove(int x, int y, int dx, int dy);
void __attribute__((weak)) OnMouseButtonDown(int x, int y, uint8_t button);
void __attribute__((weak)) OnMouseButtonUp(int x, int y, uint8_t button);

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// PS2 Controller Commands
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_AUX     0xA7
#define PS2_CMD_ENABLE_AUX      0xA8
#define PS2_CMD_TEST_AUX        0xA9
#define PS2_CMD_WRITE_AUX       0xD4

// Mouse Commands
#define MOUSE_CMD_RESET         0xFF
#define MOUSE_CMD_ENABLE        0xF4
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_SET_SAMPLE    0xF3

//keyboard
void PS2Init(void);
// Unified PS/2 interrupt handler
void PS2Handler(void);
int GetMouseX(void);
int GetMouseY(void);
int GetMouseDeltaX(void);
int GetMouseDeltaY(void);
uint8_t GetMouseButtons(void);
int IsLeftButtonPressed(void);
int IsRightButtonPressed(void);
int IsMiddleButtonPressed(void);
#endif