#ifndef VOIDFRAME_STRINGOPS_H
#define VOIDFRAME_STRINGOPS_H
#include "stdint.h"

extern int FastStrCmp(const char* str1, const char* str2);
extern size_t FastStrlen(const char* s, size_t max);
extern void FastStrCopy(char* dst, const char* src, size_t max_len);
extern const char* FastStrChr(const char* str, int c);
extern int StringLength(const char* str);
extern int FastStrnCmp(const char* str1, const char* str2, size_t n);
extern char* strtok(char* s, char d);
extern void strncpy(char* dest, const char* src, size_t max_len);
extern void strcpy(char* dest, const char* src);
extern void strcat(char* dest, const char* src);
extern void htoa(uint64_t n, char* buffer);
extern void itoa(uint64_t n, char* buffer);
extern char * strsep(char **s, const char *ct);
extern char * strpbrk(const char * cs,const char * ct);
extern size_t strspn(const char *s, const char *accept);

#endif // VOIDFRAME_STRINGOPS_H
