#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "stdint.h"

void KeyboardInit(void);
void KeyboardHandler(void);
char GetChar(void);
int HasInput(void);

#endif