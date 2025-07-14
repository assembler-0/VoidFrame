#ifndef KERNEL_H
#define KERNEL_H
#include "stdint.h"
void ClearScreen();
void PrintKernel(const char* str);
void PrintKernelHex(uint64_t num);
void PrintKernelInt(int64_t num);
void PrintKernelAt(const char* str, uint32_t line, uint32_t col);

// Compatibility functions for existing code
void PrintKernelHex32(uint32_t num);
void PrintKernelInt32(int32_t num);

// New colored output functions
void PrintKernelSuccess(const char* str);
void PrintKernelError(const char* str);
void PrintKernelWarning(const char* str);
void ParseMultibootInfo(uint32_t info);
#endif
