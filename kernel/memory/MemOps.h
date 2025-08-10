#ifndef MEMOPS_H
#define MEMOPS_H

#include "stdint.h"

void* FastMemset(void* dest, int value, uint64_t size);
void* FastMemcpy(void* dest, const void* src, uint64_t size);
int FastMemcmp(const void* ptr1, const void* ptr2, uint64_t size);
void FastZeroPage(void* page);

void strcpy(char* dest, const char* src);
void strcat(char* dest, const char* src);
void htoa(uint64_t n, char* buffer);
void itoa(uint64_t n, char* buffer);

#endif