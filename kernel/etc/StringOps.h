#ifndef VOIDFRAME_STRINGOPS_H
#define VOIDFRAME_STRINGOPS_H
#include <stdint.h>

int FastStrCmp(const char* str1, const char* str2);
size_t FastStrlen(const char* s, size_t max);
void FastStrCopy(char* dst, const char* src, size_t max_len);
const char* FastStrChr(const char* str, int c);
int StringLength(const char* str);
int FastStrnCmp(const char* str1, const char* str2, size_t n);
char* strtok(char* s, char d);
void strncpy(char* dest, const char* src, size_t max_len);
void strcpy(char* dest, const char* src);
void strcat(char* dest, const char* src);
void htoa(uint64_t n, char* buffer);
void itoa(uint64_t n, char* buffer);
char * strsep(char **s, const char *ct);
char * strpbrk(const char * cs,const char * ct);
size_t strspn(const char *s, const char *accept);

#endif // VOIDFRAME_STRINGOPS_H
