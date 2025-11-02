#ifndef MEMOPS_H
#define MEMOPS_H

#include <stdint.h>

void* FastMemset(void* restrict dest, int value, uint64_t size);
void* FastMemcpy(void* restrict dest, const void* restrict src, uint64_t size);
int FastMemcmp(const void* restrict ptr1, const void* restrict ptr2, uint64_t size);
void FastZeroPage(void* restrict page);

// Wrapper for host compilers
void* memset(void* restrict dest, int value, unsigned long size);
void* memcpy(void* restrict dest, const void* restrict src, unsigned long size);
int memcmp(const void* restrict s1, const void* restrict s2, unsigned long);

#endif