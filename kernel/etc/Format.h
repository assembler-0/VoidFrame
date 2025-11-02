#pragma once
#include <stdarg.h>
#include <stddef.h>

#define FORMAT_STACK_SIZE 1024

/**
 * @brief Formats a string according to the given format string and variable argument list,
 * storing the result into the provided buffer with a specified maximum size.
 *
 * This function handles various format specifiers including:
 * - `%d`, `%i` for signed integers
 * - `%u` for unsigned integers
 * - `%x`, `%X` for hexadecimal integers (lowercase and uppercase)
 * - `%o` for octal representation
 * - `%p` for pointers
 * - `%s` for strings
 * - `%c` for characters
 * - `%%` for a literal '%' character
 *
 * Any unknown format specifiers are written as-is into the buffer,
 * prefixed by a literal '%' character.
 *
 * If the buffer size is insufficient, the output is truncated to fit within
 * the buffer size, leaving the last space for a null-terminator.
 *
 * If the buffer or format string is invalid, or if the size is zero, the function
 * returns immediately without writing to the buffer.
 *
 * @param buffer Pointer to the character array where the formatted string will be stored.
 * @param size Size of the buffer, including space for the null-terminator.
 * @param fmt Pointer to a null-terminated format string.
 * @param args Variable argument list containing the data to format.
 * @return The number of characters written to the buffer, excluding the null-terminator.
 *         Returns -1 if the buffer is null or the size is zero.
 */
int vsnprintf(char *buffer, size_t size, const char *fmt, va_list args);

/**
 * @alias vsnprintf
 * @brief Convenience wrapper for the Format (...) function using variable arguments.
 * @param buffer Buffer to the character out
 * @param size size of buffer
 * @param format The format string itself
 * @param ... Format param(s)
 * @return
 */
int snprintf(char* buffer, size_t size, const char* format, ...);

/**
 * @brief An UNSAFE version and deprecated version of Format wrapper
 * @param format The format string
 * @param ... Format param(s)
 * @return The formatted string
 * @warning please use the formatted string IMMEDIATELY, any further concurrent calls will overwrite the static buffer
 */
char * FormatS(const char* format, ...);