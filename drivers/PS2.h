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

//keyboard
void PS2Init(void);
void KeyboardHandler(void);
char GetChar(void);
int HasInput(void);
//mouse
void send_mouse_command(uint8_t cmd);
void MouseHandler(void);
int GetMouseX(void);
int GetMouseY(void);
int GetMouseDeltaX(void);
int GetMouseDeltaY(void);
uint8_t GetMouseButtons(void);
int IsLeftButtonPressed(void);
int IsRightButtonPressed(void);
int IsMiddleButtonPressed(void);
#endif