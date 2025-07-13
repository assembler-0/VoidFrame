#ifndef PANIC_H
#define PANIC_H

#include "stdint.h"

// Panic function - never returns
void __attribute__((noreturn)) Panic(const char* message);
void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code);

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#define ASSERT(condition) \
    do { \
        if (!(condition)) { \
            Panic("Assertion failed: " #condition " at " __FILE__ ":" STRINGIFY(__LINE__)); \
        } \
    } while(0)

#endif