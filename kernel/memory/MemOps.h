#ifndef MEMOPS_H
#define MEMOPS_H

#include "stdint.h"

void* FastMemset(void* dest, int value, uint64_t size);
void* FastMemcpy(void* dest, const void* src, uint64_t size);
int FastMemcmp(const void* ptr1, const void* ptr2, uint64_t size);
void FastZeroPage(void* page);
int FastStrCmp(const char* str1, const char* str2);

#endif