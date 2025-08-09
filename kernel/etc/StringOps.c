#include "StringOps.h"

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