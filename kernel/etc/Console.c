#include "Console.h"
#include "Io.h"
#include "Serial.h"
#include "Spinlock.h"
#include "stdbool.h"
#include "stdint.h"

static void UpdateCursor(void) {
    uint16_t pos = console.line * VGA_WIDTH + console.column;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static volatile int lock = 0;
// Inline functions for better performance
static void ConsoleSetColor(uint8_t color) {
    console.color = color;
}

static inline uint16_t MakeVGAEntry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void ConsolePutcharAt(char c, uint32_t x, uint32_t y, uint8_t color) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    const uint32_t index = y * VGA_WIDTH + x;
    console.buffer[index] = MakeVGAEntry(c, color);
}

// Optimized screen clear using memset-like approach
void ClearScreen(void) {
    SpinLock(&lock);
    const uint16_t blank = MakeVGAEntry(' ', VGA_COLOR_DEFAULT);

    // Use 32-bit writes for better performance
    volatile uint32_t* buffer32 = (volatile uint32_t*)console.buffer;
    const uint32_t blank32 = ((uint32_t)blank << 16) | blank;
    const uint32_t size32 = (VGA_WIDTH * VGA_HEIGHT) / 2;

    for (uint32_t i = 0; i < size32; i++) {
        buffer32[i] = blank32;
    }

    console.line = 0;
    console.column = 0;
    UpdateCursor();
    SpinUnlock(&lock);;
}

// Optimized scrolling
static void ConsoleScroll(void) {
    // Move all lines up by one using memmove-like operation
    for (uint32_t i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        console.buffer[i] = console.buffer[i + VGA_WIDTH];
    }

    // Clear the last line
    const uint16_t blank = MakeVGAEntry(' ', console.color);
    const uint32_t last_line_start = (VGA_HEIGHT - 1) * VGA_WIDTH;

    for (uint32_t i = 0; i < VGA_WIDTH; i++) {
        console.buffer[last_line_start + i] = blank;
    }
}

// Optimized character output with bounds checking
static void ConsolePutchar(char c) {
    if (c == '\n') {
        console.line++;
        console.column = 0;
    } else if (c == '\r') {
        console.column = 0;
    } else if (c == '\t') {
        console.column = (console.column + 8) & ~7; // Align to 8
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    } else if (c == '\b') {
        if (console.column > 0) {
            console.column--;
            ConsolePutcharAt(' ' , console.column, console.line, console.color);
        } else if (console.line > 0) {
            console.line--;
            console.column = VGA_WIDTH - 1;
            ConsolePutcharAt(' ', console.column, console.line, console.color);
        }
        // Do not scroll on backspace
    } else if (c >= 32) { // Printable characters only
        ConsolePutcharAt(c, console.column, console.line, console.color);
        console.column++;
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    }

    // Handle scrolling
    if (console.line >= VGA_HEIGHT) {
        ConsoleScroll();
        console.line = VGA_HEIGHT - 1;
    }
    
    UpdateCursor();
}

// Modern string output with length checking
void PrintKernel(const char* str) {
    if (!str) return;
    SpinLock(&lock);
    // Cache the original color
    const uint8_t original_color = console.color;

    for (const char* p = str; *p; p++) {
        ConsolePutchar(*p);
    }

    console.color = original_color;
    SpinUnlock(&lock);
    SerialWrite(str);
}

// Colored output variants
void PrintKernelSuccess(const char* str) {
    ConsoleSetColor(VGA_COLOR_SUCCESS);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelError(const char* str) {
    ConsoleSetColor(VGA_COLOR_ERROR);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelWarning(const char* str) {
    ConsoleSetColor(VGA_COLOR_WARNING);
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

// Optimized hex printing with proper formatting
void PrintKernelHex(uint64_t num) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19]; // "0x" + 16 hex digits + null terminator

    buffer[0] = '0';
    buffer[1] = 'x';

    if (num == 0) {
        buffer[2] = '0';
        buffer[3] = '\0';
        PrintKernel(buffer);
        return;
    }

    int pos = 18;
    buffer[pos--] = '\0';

    while (num > 0 && pos >= 2) {
        buffer[pos--] = hex_chars[num & 0xF];
        num >>= 4;
    }

    PrintKernel(&buffer[pos + 1]);
}

// Optimized integer printing with proper sign handling
void PrintKernelInt(int64_t num) {
    char buffer[21]; // Max digits for 64-bit signed integer + sign + null

    if (num == 0) {
        PrintKernel("0");
        return;
    }

    bool negative = num < 0;
    if (negative) num = -num;

    int pos = 20;
    buffer[pos--] = '\0';

    while (num > 0 && pos >= 0) {
        buffer[pos--] = '0' + (num % 10);
        num /= 10;
    }

    if (negative && pos >= 0) {
        buffer[pos--] = '-';
    }

    PrintKernel(&buffer[pos + 1]);
}

// Safe positioned printing
void PrintKernelAt(const char* str, uint32_t line, uint32_t col) {
    if (!str || line >= VGA_HEIGHT || col >= VGA_WIDTH) return;

    const uint32_t saved_line = console.line;
    const uint32_t saved_col = console.column;

    console.line = line;
    console.column = col;

    // Print until end of line or string
    for (const char* p = str; *p && console.column < VGA_WIDTH; p++) {
        if (*p == '\n') break;
        ConsolePutchar(*p);
    }

    // Restore cursor position
    console.line = saved_line;
    console.column = saved_col;
}

