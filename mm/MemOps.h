#ifndef MEMOPS_H
#define MEMOPS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* FastMemset(void* dest, int value, uint64_t size);
void* FastMemcpy(void* dest, const void* src, uint64_t size);
int FastMemcmp(const void* ptr1, const void* ptr2, uint64_t size);
void FastZeroPage(void* page);

// Wrapper for host compilers
void* memset(void* dest, int value, unsigned long size);
void* memcpy(void* dest, const void* src, unsigned long size);
int memcmp(const void* s1, const void* s2, unsigned long);

#ifdef __cplusplus
}
#endif

#endif