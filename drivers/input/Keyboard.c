#include <Keyboard.h>
#include <PS2.h>
#include <usb/hid/USBKeyboard.h>

/**
 * @brief Checks for input, prioritizing USB keyboards over PS/2.
 */
int HasInput(void) {
    if (USB_Keyboard_HasInput()) {
        return 1;
    }
    if (PS2_HasInput()) {
        return 1;
    }
    return 0;
}

/**
 * @brief Gets a character, prioritizing USB keyboards over PS/2.
 */
char GetChar(void) {
    if (USB_Keyboard_HasInput()) {
        return USB_Keyboard_GetChar();
    }
    if (PS2_HasInput()) {
        return PS2_GetChar();
    }
    return 0;
}
