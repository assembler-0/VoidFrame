#ifndef VOIDFRAME_STRINGOPS_H
#define VOIDFRAME_STRINGOPS_H
#include "stdint.h"

int FastStrCmp(const char* str1, const char* str2);
size_t FastStrlen(const char* s, size_t max);
void FastStrCopy(char* dst, const char* src, size_t max_len);
const char* FastStrChr(const char* str, int c);
int StringLength(const char* str);
#endif // VOIDFRAME_STRINGOPS_H
