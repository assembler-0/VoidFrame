#ifndef KEYBOARD_H
#define KEYBOARD_H

/**
 * @brief Checks if there is any character input available from any keyboard.
 * @return 1 if input is available, 0 otherwise.
 */
int HasInput(void);

/**
 * @brief Gets the next character from the keyboard input buffer.
 *        It checks all available keyboard drivers (USB, PS/2).
 * @return The ASCII character, or 0 if no input is available.
 */
char GetChar(void);

#endif // KEYBOARD_H
