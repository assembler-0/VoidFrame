#include <stdint.h>
#include <Compositor.h>

#ifndef PS2_H
#define PS2_H

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

// Modifier flags for combos
#define K_SHIFT 0x01
#define K_CTRL  0x02
#define K_ALT   0x04
#define K_SUPER 0x08

// Compute the resulting character for a given modifier combo and base char
char PS2_CalcCombo(uint8_t mods, char base);

// Fast helper for control-key combos: returns ASCII control code for letters
static inline char PS2_Ctrl(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 1);
    if (c >= 'A' && c <= 'Z') return (char)(c - 'A' + 1);
    return c;
}


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
char PS2_GetChar(void);
int PS2_HasInput(void);
#endif