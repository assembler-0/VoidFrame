#ifndef VOIDFRAME_CONSOLE_H
#define VOIDFRAME_CONSOLE_H

#define VGA_BUFFER_ADDR     0xB8000
#define VGA_WIDTH           80
#define VGA_HEIGHT          25
#define VGA_BUFFER_SIZE     (VGA_WIDTH * VGA_HEIGHT)
#define VGA_COLOR_DEFAULT   0x08
#define VGA_COLOR_SUCCESS   0x0B
#define VGA_COLOR_ERROR     0x0C
#define VGA_COLOR_WARNING   0x0E
#include "stdint.h"
// Console state
typedef struct {
  uint32_t line;
  uint32_t column;
  volatile uint16_t* buffer;
  uint8_t color;
} ConsoleT;

typedef enum {
  INIT_SUCCESS = 0,
  INIT_ERROR_GDT,
  INIT_ERROR_IDT,
  INIT_ERROR_SYSCALL,
  INIT_ERROR_PIC,
  INIT_ERROR_MEMORY,
  INIT_ERROR_PROCESS,
  INIT_ERROR_SECURITY
} InitResultT;

extern ConsoleT console;

void PrintKernelSuccess(const char* str);
void PrintKernelError(const char* str);
void PrintKernelWarning(const char* str);
void PrintKernelHex(uint64_t num);
void PrintKernel(const char* str);
void PrintKernelInt(int64_t num);
void PrintKernelAt(const char* str, uint32_t line, uint32_t col);
void PrintKernelN(const char* str, uint32_t n);
void ClearScreen();

#endif // VOIDFRAME_CONSOLE_H
