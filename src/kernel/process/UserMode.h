#ifndef USERMODE_H
#define USERMODE_H

#include "stdint.h"

void JumpToUserMode(void (*user_function)(void));
void CreateUserProcess(void (*user_function)(void));

#endif