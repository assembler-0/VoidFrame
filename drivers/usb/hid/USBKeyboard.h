#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <../../xHCI/xHCI.h>

// Represents the 8-byte report sent by a standard USB HID keyboard.
typedef struct {
    uint8_t modifiers;    // Ctrl, Shift, Alt, GUI
    uint8_t reserved;
    uint8_t keycodes[6];  // Up to 6 simultaneous key presses
} USBHIDKeyboardReport;

// Initializes the USB keyboard device.
// This will be called by the xHCI driver when a keyboard is detected.
void USBKeyboardInit(XhciController* controller, uint8_t slot_id);
void USBKeyboardHandleInput(USBHIDKeyboardReport* report);
// Functions to be called by the kernel's input system
char USB_Keyboard_GetChar(void);
int USB_Keyboard_HasInput(void);

#endif //USB_KEYBOARD_H
