#pragma once
#include "stdarg.h"
#include "stddef.h"

#define FORMAT_STACK_SIZE 2048

int Format(char* buffer, size_t size, const char* format, va_list args);
int FormatA(char* buffer, size_t size, const char* format, ...);
char* FormatS(const char* format, ...);