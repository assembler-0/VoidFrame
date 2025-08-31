#include "StringOps.h"

int StringLength(const char* str) { // simpler than FasStrlen,
    if (!str) return 0;
    int len = 0;
    while (str[len]) len++;
    return len;
}

size_t FastStrlen(const char* s, size_t max) {
    if (!s) return 0;
    size_t i = 0;
    while (i < max && s[i]) i++;
    return i;
}

void FastStrCopy(char* dst, const char* src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    for (; i + 1 < max_len && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

int FastStrCmp(const char* str1, const char* str2) {
    if (!str1 || !str2) return (str1 == str2) ? 0 : (str1 ? 1 : -1);

    // Simple byte-by-byte comparison to avoid alignment issues
    while (*str1 && *str1 == *str2) {
        str1++;
        str2++;
    }

    return (unsigned char)*str1 - (unsigned char)*str2;
}

const char* FastStrChr(const char* str, int c) {
    char target = (char)c;

    // Loop until we hit the null terminator
    while (*str != '\0') {
        if (*str == target) {
            // Found the character, return a pointer to it
            return str;
        }
        str++;
    }

    // Special case: if the character we're looking for IS the null terminator,
    // the standard says we should return a pointer to it.
    if (target == '\0') {
        return str;
    }

    // If we get here, the character wasn't found
    return NULL;
}

void strncpy(char* dest, const char* src, size_t max_len) {
    if (!dest || !src) return;
    size_t i = 0;
    for (; i + 1 < max_len && src[i]; i++) dest[i] = src[i];
    dest[i] = '\0';
}

void strcpy(char* dest, const char* src) {
    if (!dest || !src) return;
    // Optimize for 64-bit aligned copies when possible
    if (((uintptr_t)dest & 7) == 0 && ((uintptr_t)src & 7) == 0) {
        uint64_t* d64 = (uint64_t*)dest;
        const uint64_t* s64 = (const uint64_t*)src;

        uint64_t val;
        while ((val = *s64++) != 0) {
            // Check if any byte in the 64-bit value is zero
            if ((val & 0xFF00000000000000ULL) == 0 ||
                (val & 0x00FF000000000000ULL) == 0 ||
                (val & 0x0000FF0000000000ULL) == 0 ||
                (val & 0x000000FF00000000ULL) == 0 ||
                (val & 0x00000000FF000000ULL) == 0 ||
                (val & 0x0000000000FF0000ULL) == 0 ||
                (val & 0x000000000000FF00ULL) == 0 ||
                (val & 0x00000000000000FFULL) == 0) {
                // Found null terminator, fall back to byte copy
                char* d = (char*)d64;
                const char* s = (const char*)(s64 - 1);
                while ((*d++ = *s++));
                return;
            }
            *d64++ = val;
        }
        *(char*)d64 = '\0';
    } else {
        // Original byte-by-byte copy for unaligned data
        while ((*dest++ = *src++));
    }
}

void strcat(char* dest, const char* src) {
    if (!dest || !src) return;
    while (*dest) dest++;
    strcpy(dest, src);  // Reuse optimized strcpy
}

void htoa(uint64_t n, char* buffer) {
    if (!buffer) return;
    static const char hex_chars[16] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';

    // Unroll the loop for better performance
    buffer[2]  = hex_chars[(n >> 60) & 0xF];
    buffer[3]  = hex_chars[(n >> 56) & 0xF];
    buffer[4]  = hex_chars[(n >> 52) & 0xF];
    buffer[5]  = hex_chars[(n >> 48) & 0xF];
    buffer[6]  = hex_chars[(n >> 44) & 0xF];
    buffer[7]  = hex_chars[(n >> 40) & 0xF];
    buffer[8]  = hex_chars[(n >> 36) & 0xF];
    buffer[9]  = hex_chars[(n >> 32) & 0xF];
    buffer[10] = hex_chars[(n >> 28) & 0xF];
    buffer[11] = hex_chars[(n >> 24) & 0xF];
    buffer[12] = hex_chars[(n >> 20) & 0xF];
    buffer[13] = hex_chars[(n >> 16) & 0xF];
    buffer[14] = hex_chars[(n >> 12) & 0xF];
    buffer[15] = hex_chars[(n >> 8)  & 0xF];
    buffer[16] = hex_chars[(n >> 4)  & 0xF];
    buffer[17] = hex_chars[n & 0xF];
    buffer[18] = '\0';
}

void itoa(uint64_t n, char* buffer) {
    if (n == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    char temp_buffer[21];
    char* p = &temp_buffer[20];
    *p = '\0';

    // Use faster division by avoiding modulo when possible
    while (n >= 10) {
        uint64_t q = n / 10;
        *--p = '0' + (n - q * 10);  // Faster than n % 10
        n = q;
    }
    *--p = '0' + n;

    strcpy(buffer, p);
}