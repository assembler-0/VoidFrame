#include "Console.h"
#include "Format.h"
#include "Io.h"
#include "Serial.h"
#include "Spinlock.h"
#include "VBEConsole.h"
#include "Vesa.h"
#include "stdarg.h"
#include "stdbool.h"
#include "stdint.h"

// VBE mode flag
static uint8_t use_vbe = 0;
// Original VGA implementation preserved
static void UpdateCursor(void) {
    if (use_vbe) return; // VBE handles cursor internally

    uint16_t pos = console.line * VGA_WIDTH + console.column;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

ConsoleT console = {
    .line = 0,
    .column = 0,
    .buffer = NULL,  // Initialize as NULL, set based on mode
    .color = VGA_COLOR_DEFAULT
};

static volatile int lock = 0;

// Initialize console - auto-detect VBE or VGA
void ConsoleInit(void) {
    if (VBEIsInitialized()) {
        use_vbe = 1;
        VBEConsoleInit();
    } else {
        use_vbe = 0;
        console.buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;
        ClearScreen();
    }
}

static inline uint16_t MakeVGAEntry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void ConsolePutcharAt(char c, uint32_t x, uint32_t y, uint8_t color) {
    if (use_vbe) return; // VBE handles this differently

    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    const uint32_t index = y * VGA_WIDTH + x;
    console.buffer[index] = MakeVGAEntry(c, color);
}

void ClearScreen(void) {
    SpinLock(&lock);

    if (use_vbe) {
        VBEConsoleClear();
    } else {
        if (!console.buffer) console.buffer = (volatile uint16_t*)VGA_BUFFER_ADDR;

        const uint16_t blank = MakeVGAEntry(' ', VGA_COLOR_DEFAULT);
        volatile uint32_t* buffer32 = (volatile uint32_t*)console.buffer;
        const uint32_t blank32 = ((uint32_t)blank << 16) | blank;
        const uint32_t size32 = (VGA_WIDTH * VGA_HEIGHT) / 2;

        for (uint32_t i = 0; i < size32; i++) {
            buffer32[i] = blank32;
        }

        console.line = 0;
        console.column = 0;
        UpdateCursor();
    }

    SpinUnlock(&lock);
}

static void ConsoleScroll(void) {
    if (use_vbe) return; // VBE handles scrolling internally

    for (uint32_t i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        console.buffer[i] = console.buffer[i + VGA_WIDTH];
    }

    const uint16_t blank = MakeVGAEntry(' ', console.color);
    const uint32_t last_line_start = (VGA_HEIGHT - 1) * VGA_WIDTH;

    for (uint32_t i = 0; i < VGA_WIDTH; i++) {
        console.buffer[last_line_start + i] = blank;
    }
}

static void ConsolePutchar(char c) {
    if (use_vbe) {
        VBEConsolePutChar(c);
        return;  // VBEConsole handles EVERYTHING including cursor tracking
    }

    // Original VGA code only runs when not in VBE mode
    if (c == '\n') {
        console.line++;
        console.column = 0;
    } else if (c == '\r') {
        console.column = 0;
    } else if (c == '\t') {
        console.column = (console.column + 8) & ~7;
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    } else if (c == '\b') {
        if (console.column > 0) {
            console.column--;
            ConsolePutcharAt(' ', console.column, console.line, console.color);
        }
    } else if (c >= 32) {
        ConsolePutcharAt(c, console.column, console.line, console.color);
        console.column++;
        if (console.column >= VGA_WIDTH) {
            console.line++;
            console.column = 0;
        }
    }

    if (console.line >= VGA_HEIGHT) {
        ConsoleScroll();
        console.line = VGA_HEIGHT - 1;
    }
    UpdateCursor();
}

// For colors to work properly:
void ConsoleSetColor(uint8_t color) {
    if (use_vbe) {
        VBEConsoleSetColor(color);  // Let VBEConsole handle it
    } else {
        console.color = color;
    }
}

void PrintKernel(const char* str) {
    if (!str) return;
    SpinLock(&lock);

    if (use_vbe) {
        VBEConsolePrint(str);
    } else {
        const uint8_t original_color = console.color;
        for (const char* p = str; *p; p++) {
            ConsolePutchar(*p);
        }
        console.color = original_color;
    }

    SpinUnlock(&lock);
    SerialWrite(str);
}

void PrintKernelChar(const char c) {
    // Create a temporary 2-character string on the stack
    char str[2];
    str[0] = c;
    str[1] = '\0'; // Null-terminate it
    PrintKernel(str);
}

void PrintKernelBadge(const char * str) {
    PrintKernelF("[ %s ] ", str);
}

void PrintKernelSuccess(const char* str) {
    ConsoleSetColor(VGA_COLOR_WHITE);
    PrintKernelBadge("SUCCESS");
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelError(const char* str) {
    ConsoleSetColor(VGA_COLOR_ERROR);
    PrintKernelBadge("ERROR");
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

void PrintKernelWarning(const char* str) {
    ConsoleSetColor(VGA_COLOR_WARNING);
    PrintKernelBadge("WARNING");
    PrintKernel(str);
    ConsoleSetColor(VGA_COLOR_DEFAULT);
}

// Rest of the functions remain the same...
void PrintKernelHex(uint64_t num) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19];

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

void PrintKernelF(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    Format(buffer, sizeof(buffer), format, args);
    va_end(args);
    PrintKernel(buffer);
}

void PrintKernelWarningF(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    Format(buffer, sizeof(buffer), format, args);
    va_end(args);
    PrintKernelWarning(buffer);
}

void PrintKernelErrorF(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    Format(buffer, sizeof(buffer), format, args);
    va_end(args);
    PrintKernelError(buffer);
}

void PrintKernelSuccessF(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    Format(buffer, sizeof(buffer), format, args);
    va_end(args);
    PrintKernelSuccess(buffer);
}

void SerialWriteF(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    Format(buffer, sizeof(buffer), format, args);
    va_end(args);
    SerialWrite(buffer);
}

void PrintKernelInt(int64_t num) {
    char buffer[21];

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

void PrintKernelAt(const char* str, uint32_t line, uint32_t col) {
    if (!str) return;
    SerialWrite(str);
    SerialWrite("\n");

    if (use_vbe) {
        VBEConsoleSetCursor(col, line);
        VBEConsolePrint(str);
    } else {
        if (line >= VGA_HEIGHT || col >= VGA_WIDTH) return;

        const uint32_t saved_line = console.line;
        const uint32_t saved_col = console.column;

        console.line = line;
        console.column = col;

        for (const char* p = str; *p && console.column < VGA_WIDTH; p++) {
            if (*p == '\n') break;
            ConsolePutchar(*p);
        }

        console.line = saved_line;
        console.column = saved_col;

    }
}