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