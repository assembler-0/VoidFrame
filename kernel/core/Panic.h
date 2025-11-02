#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

// Forward declare the Registers struct to avoid circular header dependencies.
// The full definition is expected in a file like "Interrupts.h".
struct Registers;
// For __builtin_expect, if not globally available
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

// Panic codes remain the same
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

// --- Public Panic API ---
void __attribute__((noreturn)) Panic(const char* message);
void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code);
void __attribute__((noreturn)) PanicWithContext(const char* message, uint64_t error_code, const char* function, const char* file, int line);

// NEW: A panic function specifically for interrupt contexts.
// It correctly captures the registers of the code that was interrupted.
void __attribute__((noreturn)) PanicFromInterrupt(const char* message, struct Registers* regs);


// Macros remain the same
#define ASSERT(condition) \
do { \
if (unlikely(!(condition))) { \
PanicWithContext("Assertion Failed: " #condition, \
PANIC_ASSERTION, \
__FUNCTION__, __FILE__, __LINE__); \
} \
} while(0)

#define PANIC(msg) \
PanicWithContext(msg, PANIC_GENERAL, __FUNCTION__, __FILE__, __LINE__)

#define PANIC_CODE(msg, code) \
PanicWithContext(msg, code, __FUNCTION__, __FILE__, __LINE__)


#endif // PANIC_H