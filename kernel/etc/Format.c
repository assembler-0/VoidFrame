#include "Format.h"
#include "../memory/MemOps.h"
#include "StringOps.h"
#include "stdarg.h"
#include "stdint.h"


void ParseFormatSpec(const char** fmt, FormatSpec* spec) {
    spec->width = 0;
    spec->precision = -1;
    spec->pad_char = ' ';
    spec->left_align = 0;
    spec->show_sign = 0;
    spec->show_prefix = 0;
    spec->zero_pad = 0;
    
    const char* f = *fmt;
    
    // Parse flags
    while (*f) {
        switch (*f) {
            case '-': spec->left_align = 1; break;
            case '+': spec->show_sign = 1; break;
            case '#': spec->show_prefix = 1; break;
            case '0': spec->zero_pad = 1; break;
            default: goto parse_width;
        }
        f++;
    }
    
parse_width:
    // Parse width
    while (*f >= '0' && *f <= '9') {
        spec->width = spec->width * 10 + (*f - '0');
        f++;
    }
    
    // Parse precision
    if (*f == '.') {
        f++;
        spec->precision = 0;
        while (*f >= '0' && *f <= '9') {
            spec->precision = spec->precision * 10 + (*f - '0');
            f++;
        }
    }
    
    // Set pad character (zero-pad only if not left-aligned and no precision)
    if (spec->zero_pad && !spec->left_align && spec->precision == -1) {
        spec->pad_char = '0';
    }
    
    *fmt = f;
}

void PadString(char* dest, const char* src, int total_width, char pad_char, int left_align) {
    int src_len = StringLength(src);
    int pad_len = total_width - src_len;

    if (pad_len <= 0) {
        strcpy(dest, src);
        return;
    }

    dest[0] = '\0';

    if (left_align) {
        strcat(dest, src);
        for (int i = 0; i < pad_len; i++) {
            int len = StringLength(dest);
            dest[len] = pad_char;
            dest[len + 1] = '\0';
        }
    } else {
        for (int i = 0; i < pad_len; i++) {
            int len = StringLength(dest);
            dest[len] = pad_char;
            dest[len + 1] = '\0';
        }
        strcat(dest, src);
    }
}

void FormatInteger(char* buffer, int64_t value, FormatSpec* spec, int base, int is_unsigned) {
    char temp_buffer[32];
    char sign_char = 0;
    uint64_t abs_value;

    if (!is_unsigned && value < 0) {
        sign_char = '-';
        abs_value = (uint64_t)(-value);
    } else {
        abs_value = (uint64_t)value;
        if (!is_unsigned && spec->show_sign && value >= 0) {
            sign_char = '+';
        }
    }

    // Convert number to string
    if (base == 16) {
        htoa(abs_value, temp_buffer);
        // Remove "0x" prefix from htoa
        char* num_part = temp_buffer + 2;
        strcpy(temp_buffer, num_part);
    } else {
        itoa(abs_value, temp_buffer);
    }

    // Handle precision (minimum digits)
    if (spec->precision > 0) {
        int num_len = StringLength(temp_buffer);
        if (num_len < spec->precision) {
            char padded[32];
            padded[0] = '\0';
            for (int i = 0; i < spec->precision - num_len; i++) {
                strcat(padded, "0");
            }
            strcat(padded, temp_buffer);
            strcpy(temp_buffer, padded);
        }
    }

    // Add prefix for hex if requested
    char final_buffer[32];
    final_buffer[0] = '\0';

    if (sign_char) {
        final_buffer[0] = sign_char;
        final_buffer[1] = '\0';
    }

    if (base == 16 && spec->show_prefix && abs_value != 0) {
        strcat(final_buffer, "0x");
    }

    strcat(final_buffer, temp_buffer);

    // Apply width and padding
    if (spec->width > 0) {
        PadString(buffer, final_buffer, spec->width, spec->pad_char, spec->left_align);
    } else {
        strcpy(buffer, final_buffer);
    }
}

void FormatString(char* buffer, const char* str, FormatSpec* spec) {
    if (!str) str = "(null)";

    char temp_buffer[512];
    strcpy(temp_buffer, str);

    // Apply precision (max characters)
    if (spec->precision >= 0) {
        temp_buffer[spec->precision] = '\0';
    }

    // Apply width and padding
    if (spec->width > 0) {
        PadString(buffer, temp_buffer, spec->width, spec->pad_char, spec->left_align);
    } else {
        strcpy(buffer, temp_buffer);
    }
}

void FormatCharacter(char* buffer, char c, FormatSpec* spec) {
    char temp_buffer[2];
    temp_buffer[0] = c;
    temp_buffer[1] = '\0';

    if (spec->width > 0) {
        PadString(buffer, temp_buffer, spec->width, spec->pad_char, spec->left_align);
    } else {
        strcpy(buffer, temp_buffer);
    }
}

char* Format(const char* format, va_list args) {
    char output_buffer[CHAR_BUFF];
    output_buffer[0] = '\0';

    const char* f = format;
    char temp_buffer[512];

    while (*f) {
        if (*f == '%') {
            f++; // Skip '%'

            if (*f == '%') {
                // Literal '%'
                strcat(output_buffer, "%");
                f++;
                continue;
            }

            FormatSpec spec;
            ParseFormatSpec(&f, &spec);

            switch (*f) {
                case 'd':
                case 'i': {
                    int value = va_arg(args, int);
                    FormatInteger(temp_buffer, value, &spec, 10, 0);
                    strcat(output_buffer, temp_buffer);
                    break;
                }

                case 'u': {
                    unsigned int value = va_arg(args, unsigned int);
                    FormatInteger(temp_buffer, value, &spec, 10, 1);
                    strcat(output_buffer, temp_buffer);
                    break;
                }

                case 'x':
                case 'X': {
                    unsigned int value = va_arg(args, unsigned int);
                    FormatInteger(temp_buffer, value, &spec, 16, 1);
                    strcat(output_buffer, temp_buffer);
                    break;
                }

                case 'p': {
                    void* ptr = va_arg(args, void*);
                    spec.show_prefix = 1;  // Always show 0x for pointers
                    FormatInteger(temp_buffer, (uintptr_t)ptr, &spec, 16, 1);
                    strcat(output_buffer, temp_buffer);
                    break;
                }

                case 's': {
                    const char* str = va_arg(args, const char*);
                    FormatString(temp_buffer, str, &spec);
                    strcat(output_buffer, temp_buffer);
                    break;
                }

                case 'c': {
                    int c = va_arg(args, int);
                    FormatCharacter(temp_buffer, (char)c, &spec);
                    strcat(output_buffer, temp_buffer);
                    break;
                }

                default:
                    // Unknown format specifier, just add it literally
                    temp_buffer[0] = *f;
                    temp_buffer[1] = '\0';
                    strcat(output_buffer, temp_buffer);
                    break;
            }

            f++;
        } else {
            // Regular character
            int len = StringLength(output_buffer);
            output_buffer[len] = *f;
            output_buffer[len + 1] = '\0';
            f++;
        }
    }
    
    return output_buffer;
}