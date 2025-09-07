#ifndef VOIDFRAME_CONSOLE_H
#define VOIDFRAME_CONSOLE_H

#define VGA_BUFFER_ADDR     0xB8000
#define VGA_WIDTH           80
#define VGA_HEIGHT          25
#define VGA_BUFFER_SIZE     (VGA_WIDTH * VGA_HEIGHT)

#define VGA_COLOR_BLACK      0x00
#define VGA_COLOR_BLUE       0x01
#define VGA_COLOR_GREEN      0x02
#define VGA_COLOR_CYAN       0x03
#define VGA_COLOR_RED        0x04
#define VGA_COLOR_MAGENTA    0x05
#define VGA_COLOR_BROWN      0x06
#define VGA_COLOR_LIGHT_GREY 0x07
#define VGA_COLOR_DARK_GREY  0x08
#define VGA_COLOR_LIGHT_BLUE 0x09
#define VGA_COLOR_LIGHT_GREEN 0x0A
#define VGA_COLOR_LIGHT_CYAN  0x0B
#define VGA_COLOR_LIGHT_RED   0x0C
#define VGA_COLOR_LIGHT_MAGENTA 0x0D
#define VGA_COLOR_LIGHT_YELLOW 0x0E
#define VGA_COLOR_WHITE       0x0F

#define VGA_COLOR_DEFAULT   VGA_COLOR_LIGHT_GREY
#define VGA_COLOR_SUCCESS   VGA_COLOR_LIGHT_GREEN
#define VGA_COLOR_ERROR     VGA_COLOR_LIGHT_RED
#define VGA_COLOR_WARNING   VGA_COLOR_LIGHT_YELLOW

#define STATUS_LABEL_ROW 29
#define STATUS_LABEL_COL 31 // future use

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
  INIT_ERROR_PIC,
  INIT_ERROR_MEMORY,
  INIT_ERROR_PROCESS,
  INIT_ERROR_SECURITY
} InitResultT;

extern ConsoleT console;

#ifdef __cplusplus
extern "C" {
#endif
void PrintKernelSuccess(const char* str);
void PrintKernelError(const char* str);
void PrintKernelWarning(const char* str);
void PrintKernelHex(uint64_t num);
void PrintKernel(const char* str);
void PrintKernelChar(char c);
void PrintKernelInt(int64_t num);
void PrintKernelAt(const char* str, uint32_t line, uint32_t col);
void ClearScreen();
void ConsoleSetColor(uint8_t color);
void ConsoleInit(void);
void Snooze();
void Unsnooze();
// formated functions
void PrintKernelF(const char* format, ...);
void SerialWriteF(const char* format, ...);
void PrintKernelErrorF(const char* format, ...);
void PrintKernelWarningF(const char* format, ...);
void PrintKernelSuccessF(const char* format, ...);
#ifdef __cplusplus
}
#endif
// save a bit of time
static inline __attribute__((always_inline)) void PrintNewline(void) {
    PrintKernel("\n");
}

#endif // VOIDFRAME_CONSOLE_H
