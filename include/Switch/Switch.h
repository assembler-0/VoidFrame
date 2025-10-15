#pragma once
#include <stdbool.h>
#include "StringOps.h"
#include "stdarg.h"
#include "stdint.h"

// ============================================================================
// CORE MACRO MAGIC - Advanced Pattern Matching Switch
// ============================================================================

// Enhanced switch with automatic type deduction and fallthrough control
#define SWITCH(x) { \
    __auto_type _switch_val = (x); \
    __auto_type _switch_type = (x); \
    bool _matched = false; \
    bool _fallthrough = false; \
    (void)_switch_type; \
    do {

#define CASE(pattern) \
    } while(0); \
    if ((_fallthrough || !_matched) && (_switch_val == (pattern))) { \
        _matched = true; _fallthrough = false;

#define WHEN(condition) \
    } while(0); \
    if ((_fallthrough || !_matched) && (condition)) { \
        _matched = true; _fallthrough = false;

#define RANGE(low, high) \
    } while(0); \
    if ((_fallthrough || !_matched) && (_switch_val >= (low) && _switch_val <= (high))) { \
        _matched = true; _fallthrough = false;

// Explicit fallthrough support
#define FALLTHROUGH _fallthrough = true;

#define DEFAULT \
    } while(0); \
    if (!_matched) { \
        _matched = true; _fallthrough = false;

#define END_SWITCH \
    } while(0); \
}

// ============================================================================
// ADVANCED: Multiple value and bitwise matching
// ============================================================================

#define CASE_ANY(...) \
    } while(0); \
    if ((_fallthrough || !_matched) && _match_any(_switch_val, __VA_ARGS__)) { \
        _matched = true; _fallthrough = false;

// Bitwise pattern matching
#define CASE_BITS(mask, expected) \
    } while(0); \
    if ((_fallthrough || !_matched) && ((_switch_val & (mask)) == (expected))) { \
        _matched = true; _fallthrough = false;

// Type-safe variadic matching with compile-time count
#define _match_any(val, ...) ({ \
    __auto_type _arr[] = {__VA_ARGS__}; \
    bool _found = false; \
    for (size_t _i = 0; _i < sizeof(_arr)/sizeof(_arr[0]); _i++) { \
        if (val == _arr[_i]) { _found = true; break; } \
    } \
    _found; \
})

// Predicate-based matching
#define CASE_IF(predicate_func) \
    } while(0); \
    if ((_fallthrough || !_matched) && (predicate_func)(_switch_val)) { \
        _matched = true; _fallthrough = false;

// ============================================================================
// STRING MATCHING with hash optimization
// ============================================================================

#define STR_SWITCH(s) { \
    const char *_str_val = (s); \
    uint32_t _str_hash = _str_val ? _djb2_hash(_str_val) : 0; \
    bool _matched = false; \
    bool _fallthrough = false; \
    do {

#define STR_CASE(str) \
    } while(0); \
    if ((_fallthrough || !_matched) && _str_val && \
        _str_hash == _djb2_hash(str) && FastStrCmp(_str_val, str) == 0) { \
        _matched = true; _fallthrough = false;

#define STR_CASE_ANY(...) \
    } while(0); \
    if ((_fallthrough || !_matched) && _str_val && _str_match_any(_str_val, _str_hash, __VA_ARGS__)) { \
        _matched = true; _fallthrough = false;

// Prefix matching
#define STR_PREFIX(prefix) \
    } while(0); \
    if ((_fallthrough || !_matched) && _str_val && _str_starts_with(_str_val, prefix)) { \
        _matched = true; _fallthrough = false;

// Fast DJB2 hash for string comparison optimization
static inline uint32_t _djb2_hash(const char *str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static inline bool _str_starts_with(const char *str, const char *prefix) {
    while (*prefix && *str == *prefix) {
        str++; prefix++;
    }
    return *prefix == '\0';
}

static bool _str_match_any(const char *val, uint32_t hash, ...) {
    va_list args;
    va_start(args, hash);
    const char *pattern;
    while ((pattern = va_arg(args, const char*)) != NULL) {
        if (hash == _djb2_hash(pattern) && FastStrCmp(val, pattern) == 0) {
            va_end(args);
            return true;
        }
    }
    va_end(args);
    return false;
}

// ============================================================================
// ADVANCED PATTERN DESTRUCTURING
// ============================================================================

// Struct field matching with destructuring
#define MATCH_STRUCT(structval) { \
    __auto_type _struct_val = (structval); \
    bool _matched = false; \
    bool _fallthrough = false; \
    do {

#define PATTERN(condition) WHEN(condition)

// Extract and match struct fields
#define EXTRACT(field, var) __auto_type var = _struct_val.field;

// Pointer pattern matching with null safety
#define PTR_SWITCH(ptr) { \
    __auto_type _ptr_val = (ptr); \
    bool _matched = false; \
    bool _fallthrough = false; \
    do {

#define PTR_NULL \
    } while(0); \
    if ((_fallthrough || !_matched) && (_ptr_val == NULL)) { \
        _matched = true; _fallthrough = false;

#define PTR_VALID \
    } while(0); \
    if ((_fallthrough || !_matched) && (_ptr_val != NULL)) { \
        _matched = true; _fallthrough = false;

// Array pattern matching
#define ARRAY_SWITCH(arr, len) { \
    __auto_type _arr_val = (arr); \
    size_t _arr_len = (len); \
    bool _matched = false; \
    bool _fallthrough = false; \
    do {

#define ARRAY_EMPTY \
    } while(0); \
    if ((_fallthrough || !_matched) && (_arr_len == 0)) { \
        _matched = true; _fallthrough = false;

#define ARRAY_SIZE(size) \
    } while(0); \
    if ((_fallthrough || !_matched) && (_arr_len == (size))) { \
        _matched = true; _fallthrough = false;

// Enum-style matching with type safety
#define ENUM_SWITCH(enumval) SWITCH(enumval)

// Result/Option-style matching
#define RESULT_SWITCH(result_ptr) { \
    __auto_type _result = (result_ptr); \
    bool _matched = false; \
    bool _fallthrough = false; \
    do {

#define OK(var) \
    } while(0); \
    if ((_fallthrough || !_matched) && _result && _result->is_ok) { \
        __auto_type var = _result->value; \
        _matched = true; _fallthrough = false;

#define ERR(var) \
    } while(0); \
    if ((_fallthrough || !_matched) && _result && !_result->is_ok) { \
        __auto_type var = _result->error; \
        _matched = true; _fallthrough = false;

// ============================================================================
// UTILITY MACROS
// ============================================================================

// Break out of switch early
#define BREAK_SWITCH goto _switch_end; _switch_end:

// Multiple condition matching
#define CASE_ALL(cond1, cond2, ...) \
    } while(0); \
    if ((_fallthrough || !_matched) && (cond1) && (cond2) && _all_true(__VA_ARGS__)) { \
        _matched = true; _fallthrough = false;

#define _all_true(...) ({ \
    bool _conditions[] = {__VA_ARGS__}; \
    bool _all = true; \
    for (size_t _i = 0; _i < sizeof(_conditions)/sizeof(_conditions[0]); _i++) { \
        if (!_conditions[_i]) { _all = false; break; } \
    } \
    _all; \
})