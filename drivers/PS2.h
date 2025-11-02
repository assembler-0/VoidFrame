#ifndef VOIDFRAME_PS2_H
#define VOIDFRAME_PS2_H

#include <stdint.h>

// PS/2 Controller ports
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

// PS/2 Commands
#define PS2_CMD_READ_CONFIG   0x20
#define PS2_CMD_WRITE_CONFIG  0x60
#define PS2_CMD_DISABLE_AUX   0xA7
#define PS2_CMD_ENABLE_AUX    0xA8
#define PS2_CMD_WRITE_AUX     0xD4

// Mouse commands
#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_ENABLE       0xF4

// Modifier key flags
#define K_SHIFT 0x01
#define K_CTRL  0x02
#define K_ALT   0x04

// PS/2 functions
void PS2Init(void);
void PS2Handler(void);

// Keyboard functions
char PS2_GetChar(void);
int PS2_HasInput(void);
char PS2_CalcCombo(uint8_t mods, char base);

// Mouse functions
int GetMouseX(void);
int GetMouseY(void);
int GetMouseDeltaX(void);
int GetMouseDeltaY(void);
uint8_t GetMouseButtons(void);
int IsLeftButtonPressed(void);
int IsRightButtonPressed(void);
int IsMiddleButtonPressed(void);

// Helper function for control characters
static inline char PS2_Ctrl(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    return c;
}

#endif // VOIDFRAME_PS2_H