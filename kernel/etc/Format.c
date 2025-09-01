#include "Format.h"


#include "stdarg.h"
#include "stddef.h"
#include "stdint.h"

// Configuration
#define MAX_FORMAT_BUFFER 4096
#define MAX_NUMBER_BUFFER 64

typedef struct {
    int width;
    int precision;
    char pad_char;
    unsigned int left_align : 1;
    unsigned int show_sign : 1;
    unsigned int show_prefix : 1;
    unsigned int zero_pad : 1;
    unsigned int uppercase : 1;
} format_spec_t;

// Safe buffer structure
typedef struct {
    char* data;
    size_t size;
    size_t pos;
} safe_buffer_t;

// Initialize buffer
void buffer_init(safe_buffer_t* buf, char* data, size_t size) {
    buf->data = data;
    buf->size = size;
    buf->pos = 0;
    if (size > 0) {
        buf->data[0] = '\0';
    }
}

// Add character to buffer with bounds checking
int buffer_putc(safe_buffer_t* buf, char c) {
    if (buf->pos + 1 >= buf->size) {
        return -1; // Buffer full
    }
    buf->data[buf->pos++] = c;
    buf->data[buf->pos] = '\0';
    return 0;
}

// Add string to buffer with bounds checking
int buffer_puts(safe_buffer_t* buf, const char* str) {
    if (!str) return -1;

    while (*str) {
        if (buffer_putc(buf, *str++) < 0) {
            return -1; // Buffer full
        }
    }
    return 0;
}

// Add counted string to buffer
int buffer_putn(safe_buffer_t* buf, const char* str, size_t n) {
    if (!str) return -1;

    for (size_t i = 0; i < n && str[i]; i++) {
        if (buffer_putc(buf, str[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

// Add padding characters
int buffer_pad(safe_buffer_t* buf, char pad_char, int count) {
    for (int i = 0; i < count; i++) {
        if (buffer_putc(buf, pad_char) < 0) {
            return -1;
        }
    }
    return 0;
}

// Get remaining space in buffer
size_t buffer_remaining(safe_buffer_t* buf) {
    return buf->size > buf->pos ? buf->size - buf->pos - 1 : 0;
}

// Convert unsigned integer to string in given base
int utoa_base(uint64_t value, char* buffer, int base, int uppercase) {
    if (base < 2 || base > 36) return -1;

    const char* digits = uppercase ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                  : "0123456789abcdefghijklmnopqrstuvwxyz";

    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return 1;
    }

    char temp[MAX_NUMBER_BUFFER];
    int pos = 0;

    while (value > 0 && pos < MAX_NUMBER_BUFFER - 1) {
        temp[pos++] = digits[value % base];
        value /= base;
    }

    // Reverse the string
    for (int i = 0; i < pos; i++) {
        buffer[i] = temp[pos - 1 - i];
    }
    buffer[pos] = '\0';

    return pos;
}

// Parse format specification
const char* parse_format_spec(const char* fmt, format_spec_t* spec) {
    // Initialize spec
    spec->width = 0;
    spec->precision = -1;
    spec->pad_char = ' ';
    spec->left_align = 0;
    spec->show_sign = 0;
    spec->show_prefix = 0;
    spec->zero_pad = 0;
    spec->uppercase = 0;

    // Parse flags
    while (*fmt) {
        switch (*fmt) {
            case '-': spec->left_align = 1; fmt++; break;
            case '+': spec->show_sign = 1; fmt++; break;
            case '#': spec->show_prefix = 1; fmt++; break;
            case '0': spec->zero_pad = 1; fmt++; break;
            case ' ': if (!spec->show_sign) spec->show_sign = 2; fmt++; break;
            default: goto parse_width;
        }
    }

parse_width:
    // Parse width
    while (*fmt >= '0' && *fmt <= '9') {
        spec->width = spec->width * 10 + (*fmt - '0');
        fmt++;
    }

    // Parse precision
    if (*fmt == '.') {
        fmt++;
        spec->precision = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            spec->precision = spec->precision * 10 + (*fmt - '0');
            fmt++;
        }
    }

    // Adjust pad character
    if (spec->zero_pad && !spec->left_align && spec->precision == -1) {
        spec->pad_char = '0';
    }

    return fmt;
}

// Format integer with specification
int format_integer(safe_buffer_t* buf, int64_t value, format_spec_t* spec,
                         int base, int is_unsigned) {
    char num_buf[MAX_NUMBER_BUFFER];
    char sign_char = 0;
    uint64_t abs_value;

    // Handle sign
    if (!is_unsigned && value < 0) {
        sign_char = '-';
        abs_value = (uint64_t)(-value);
    } else {
        abs_value = (uint64_t)value;
        if (!is_unsigned && spec->show_sign && value >= 0) {
            sign_char = (spec->show_sign == 1) ? '+' : ' ';
        }
    }

    // Convert to string
    int num_len = utoa_base(abs_value, num_buf, base, spec->uppercase);
    if (num_len < 0) return -1;

    // Apply precision (minimum digits)
    int zero_pad_count = 0;
    if (spec->precision > num_len) {
        zero_pad_count = spec->precision - num_len;
    }

    // Calculate prefix length
    int prefix_len = 0;
    if (sign_char) prefix_len++;
    if (base == 16 && spec->show_prefix && abs_value != 0) prefix_len += 2;
    if (base == 8 && spec->show_prefix && abs_value != 0) prefix_len += 1;

    // Calculate total content length
    int content_len = prefix_len + zero_pad_count + num_len;

    // Calculate padding
    int total_pad = 0;
    if (spec->width > content_len) {
        total_pad = spec->width - content_len;
    }

    // Output with proper alignment
    if (!spec->left_align && spec->pad_char == ' ') {
        // Right align with space padding
        if (buffer_pad(buf, ' ', total_pad) < 0) return -1;
    }

    // Add sign
    if (sign_char && buffer_putc(buf, sign_char) < 0) return -1;

    // Add prefix
    if (base == 16 && spec->show_prefix && abs_value != 0) {
        if (buffer_putc(buf, '0') < 0) return -1;
        if (buffer_putc(buf, spec->uppercase ? 'X' : 'x') < 0) return -1;
    } else if (base == 8 && spec->show_prefix && abs_value != 0) {
        if (buffer_putc(buf, '0') < 0) return -1;
    }

    // Add zero padding from width (if not left-aligned and using '0' pad)
    if (!spec->left_align && spec->pad_char == '0') {
        if (buffer_pad(buf, '0', total_pad) < 0) return -1;
    }

    // Add precision zero padding
    if (buffer_pad(buf, '0', zero_pad_count) < 0) return -1;

    // Add number
    if (buffer_puts(buf, num_buf) < 0) return -1;

    // Left align padding
    if (spec->left_align) {
        if (buffer_pad(buf, ' ', total_pad) < 0) return -1;
    }

    return 0;
}

// Format string with specification
int format_string(safe_buffer_t* buf, const char* str, format_spec_t* spec) {
    if (!str) str = "(null)";

    // Calculate string length up to precision
    int str_len = 0;
    const char* s = str;
    while (*s && (spec->precision < 0 || str_len < spec->precision)) {
        str_len++;
        s++;
    }

    // Calculate padding
    int total_pad = 0;
    if (spec->width > str_len) {
        total_pad = spec->width - str_len;
    }

    // Apply padding and string
    if (!spec->left_align) {
        if (buffer_pad(buf, spec->pad_char, total_pad) < 0) return -1;
    }

    if (buffer_putn(buf, str, str_len) < 0) return -1;

    if (spec->left_align) {
        if (buffer_pad(buf, ' ', total_pad) < 0) return -1;
    }

    return 0;
}

// Main formatting function - stack-based version
int Format(char* buffer, size_t size, const char* format, va_list args) {
    if (!buffer || size == 0) return -1;

    safe_buffer_t buf;
    buffer_init(&buf, buffer, size);

    const char* f = format;

    while (*f && buffer_remaining(&buf) > 0) {
        if (*f == '%') {
            f++; // Skip '%'

            if (*f == '%') {
                // Literal '%'
                if (buffer_putc(&buf, '%') < 0) break;
                f++;
                continue;
            }

            format_spec_t spec;
            f = parse_format_spec(f, &spec);

            switch (*f) {
                case 'd':
                case 'i': {
                    int value = va_arg(args, int);
                    format_integer(&buf, value, &spec, 10, 0);
                    break;
                }

                case 'u': {
                    unsigned int value = va_arg(args, unsigned int);
                    format_integer(&buf, value, &spec, 10, 1);
                    break;
                }

                case 'x': {
                    unsigned int value = va_arg(args, unsigned int);
                    spec.uppercase = 0;
                    format_integer(&buf, value, &spec, 16, 1);
                    break;
                }

                case 'X': {
                    unsigned int value = va_arg(args, unsigned int);
                    spec.uppercase = 1;
                    format_integer(&buf, value, &spec, 16, 1);
                    break;
                }

                case 'o': {
                    unsigned int value = va_arg(args, unsigned int);
                    format_integer(&buf, value, &spec, 8, 1);
                    break;
                }

                case 'p': {
                    void* ptr = va_arg(args, void*);
                    spec.show_prefix = 1;
                    spec.uppercase = 0;
                    format_integer(&buf, (uintptr_t)ptr, &spec, 16, 1);
                    break;
                }

                case 's': {
                    const char* str = va_arg(args, const char*);
                    format_string(&buf, str, &spec);
                    break;
                }

                case 'c': {
                    int c = va_arg(args, int);
                    char temp[2] = {(char)c, '\0'};
                    format_string(&buf, temp, &spec);
                    break;
                }

                default:
                    // Unknown format specifier
                    buffer_putc(&buf, '%');
                    buffer_putc(&buf, *f);
                    break;
            }

            if (*f) f++;
        } else {
            // Regular character
            if (buffer_putc(&buf, *f) < 0) break;
            f++;
        }
    }

    return buf.pos;
}

// Convenience wrapper
int FormatA(char* buffer, size_t size, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int result = Format(buffer, size, format, args);
    va_end(args);
    return result;
}

char* FormatS(const char* format, ...) {
    static char stack_buffer[FORMAT_STACK_SIZE];
    va_list args;
    va_start(args, format);
    Format(stack_buffer, FORMAT_STACK_SIZE, format, args);
    va_end(args);
    return stack_buffer;
}
