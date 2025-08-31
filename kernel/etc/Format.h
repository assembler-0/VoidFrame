#ifndef VOIDFRAME_FORMAT_H
#define VOIDFRAME_FORMAT_H

#include "stdarg.h"

typedef struct {
    int width;
    int precision;
    char pad_char;
    int left_align;
    int show_sign;
    int show_prefix;
    int zero_pad;
} FormatSpec;

#define CHAR_BUFF 8192

char* Format(const char* format, va_list args) ;

static inline char * FormatS(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char* formatted = Format(format, args);
    va_end(args);
    return formatted;
}

#endif // VOIDFRAME_FORMAT_H
