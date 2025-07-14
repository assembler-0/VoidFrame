#ifndef PANIC_H
#define PANIC_H

#include "stdint.h"

// For __builtin_expect, if not globally available
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Forward declarations for the public panic API
void __attribute__((noreturn)) Panic(const char* message);
void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code);
void __attribute__((noreturn)) PanicWithContext(const char* message, uint64_t error_code, const char* function, const char* file, int line);
typedef enum {
    PANIC_GENERAL = 0x0001,
    PANIC_MEMORY = 0x0002,
    PANIC_INTERRUPT = 0x0003,
    PANIC_HARDWARE = 0x0004,
    PANIC_FILESYSTEM = 0x0005,
    PANIC_NETWORK = 0x0006,
    PANIC_SECURITY = 0x0007,
    PANIC_ASSERTION = 0x0008
} PanicCode;

/**
 * @brief Halts the kernel if a condition is not met.
 *
 * This is a critical assertion. If the condition is false, the kernel will
 * immediately panic, displaying the failed condition, function, file, and line.
 */
#define ASSERT(condition) \
do { \
if (unlikely(!(condition))) { \
PanicWithContext("Assertion Failed: " #condition, \
PANIC_ASSERTION, \
__FUNCTION__, __FILE__, __LINE__); \
} \
} while(0)


/**
 * @brief Unconditionally halts the kernel.
 *
 * Displays the given message and context information (function, file, line).
 */
#define PANIC(msg) \
PanicWithContext(msg, PANIC_GENERAL, __FUNCTION__, __FILE__, __LINE__)


/**
 * @brief Unconditionally halts the kernel with a specific error code.
 *
 * Displays the given message and context information (function, file, line).
 */
#define PANIC_CODE(msg, code) \
PanicWithContext(msg, code, __FUNCTION__, __FILE__, __LINE__)


#endif // PANIC_H