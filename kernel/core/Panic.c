#include "Panic.h"
#include "Kernel.h"
#include "Io.h"
#include "stdint.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define VIDEO_MEMORY (0xB8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

// Red color scheme definitions
#define COLOR_RED_ON_BLACK 0x0C
#define COLOR_BRIGHT_RED_ON_BLACK 0x04
#define COLOR_WHITE_ON_RED 0x4F
#define COLOR_YELLOW_ON_RED 0x4E
#define COLOR_BLACK_ON_RED 0x40
#define COLOR_BRIGHT_WHITE_ON_RED 0x4F
#define COLOR_FLASHING_RED 0x8C

// Panic error codes
typedef enum {
    PANIC_GENERAL = 0x0001,
    PANIC_MEMORY = 0x0002,
    PANIC_INTERRUPT = 0x0003,
    PANIC_HARDWARE = 0x0004,
    PANIC_FILESYSTEM = 0x0005,
    PANIC_NETWORK = 0x0006,
    PANIC_SECURITY = 0x0007,
    PANIC_ASSERTION = 0x0008
} PanicCode;

// Panic context structure
typedef struct {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t error_code;
    uint64_t timestamp;
    const char* function;
    const char* file;
    int line;
} PanicContext;

void RedScreen() {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t red_attr = (COLOR_WHITE_ON_RED << 8);

    // Fill entire screen with red background
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_memory[i] = red_attr | ' ';
    }
}

void PanicPrint(int x, int y, const char* str, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);
    int i = 0;

    while (str[i] != '\0' && x + i < SCREEN_WIDTH) {
        video_memory[y * SCREEN_WIDTH + x + i] = attr | str[i];
        i++;
    }
}

void PanicPrintCentered(int y, const char* str, uint8_t color) {
    int len = 0;
    while (str[len] != '\0') len++;
    int x = (SCREEN_WIDTH - len) / 2;
    PanicPrint(x, y, str, color);
}

void PanicPrintHex(int x, int y, uint64_t value, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);

    const char hex[] = "0123456789ABCDEF";
    char buffer[19] = "0x";

    for (int i = 15; i >= 0; i--) {
        buffer[17 - i] = hex[(value >> (i * 4)) & 0xF];
    }
    buffer[18] = '\0';

    PanicPrint(x, y, buffer, color);
}

void DrawPanicBox(int x, int y, int width, int height, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);

    // Double-line box with heavy emphasis
    video_memory[y * SCREEN_WIDTH + x] = attr | 201; // ╔
    video_memory[y * SCREEN_WIDTH + x + width - 1] = attr | 187; // ╗
    video_memory[(y + height - 1) * SCREEN_WIDTH + x] = attr | 200; // ╚
    video_memory[(y + height - 1) * SCREEN_WIDTH + x + width - 1] = attr | 188; // ╝

    // Horizontal lines
    for (int i = 1; i < width - 1; i++) {
        video_memory[y * SCREEN_WIDTH + x + i] = attr | 205; // ═
        video_memory[(y + height - 1) * SCREEN_WIDTH + x + i] = attr | 205; // ═
    }

    // Vertical lines
    for (int i = 1; i < height - 1; i++) {
        video_memory[(y + i) * SCREEN_WIDTH + x] = attr | 186; // ║
        video_memory[(y + i) * SCREEN_WIDTH + x + width - 1] = attr | 186; // ║
    }
}

void ShowPanicHeader() {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;

    // Clear screen to red
    RedScreen();

    // Top border with danger symbols
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        video_memory[0 * SCREEN_WIDTH + i] = (COLOR_BRIGHT_RED_ON_BLACK << 8) | 219; // █
        video_memory[1 * SCREEN_WIDTH + i] = (COLOR_BRIGHT_RED_ON_BLACK << 8) | 219; // █
    }

    // Main panic message
    PanicPrintCentered(2, "KERNEL PANIC", COLOR_BRIGHT_WHITE_ON_RED);
    PanicPrintCentered(3, "CRITICAL SYSTEM FAILURE", COLOR_YELLOW_ON_RED);

    // Warning symbols using CP437 character 19 (‼) or 33 (!)
    for (int i = 0; i < 8; i++) {
        video_memory[4 * SCREEN_WIDTH + i * 10] = (COLOR_YELLOW_ON_RED << 8) | 33; // !
    }

    // Divider using CP437 double line
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        video_memory[5 * SCREEN_WIDTH + i] = (COLOR_BRIGHT_WHITE_ON_RED << 8) | 205; // ═
    }
}

void ShowPanicDetails(const char* message, uint64_t error_code, PanicContext* ctx) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    int y = 7;

    // Error message
    PanicPrint(2, y++, "ERROR: ", COLOR_BRIGHT_WHITE_ON_RED);
    PanicPrint(9, y-1, message, COLOR_WHITE_ON_RED);
    y++;

    // Error code
    PanicPrint(2, y, "CODE:  ", COLOR_BRIGHT_WHITE_ON_RED);
    PanicPrintHex(9, y++, error_code, COLOR_WHITE_ON_RED);
    y++;

    // Context information
    if (ctx) {
        PanicPrint(2, y++, "CONTEXT:", COLOR_BRIGHT_WHITE_ON_RED);
        PanicPrint(4, y, "RIP: ", COLOR_WHITE_ON_RED);
        PanicPrintHex(9, y++, ctx->rip, COLOR_WHITE_ON_RED);
        PanicPrint(4, y, "RSP: ", COLOR_WHITE_ON_RED);
        PanicPrintHex(9, y++, ctx->rsp, COLOR_WHITE_ON_RED);
        PanicPrint(4, y, "RBP: ", COLOR_WHITE_ON_RED);
        PanicPrintHex(9, y++, ctx->rbp, COLOR_WHITE_ON_RED);

        if (ctx->function) {
            PanicPrint(4, y, "FUNC: ", COLOR_WHITE_ON_RED);
            PanicPrint(10, y++, ctx->function, COLOR_WHITE_ON_RED);
        }

        if (ctx->file) {
            PanicPrint(4, y, "FILE: ", COLOR_WHITE_ON_RED);
            PanicPrint(10, y++, ctx->file, COLOR_WHITE_ON_RED);
        }
        y++;
    }

    // System status with bullet points using CP437 character 7 (•)
    PanicPrint(2, y++, "SYSTEM STATUS:", COLOR_BRIGHT_WHITE_ON_RED);
    video_memory[y * SCREEN_WIDTH + 4] = (COLOR_WHITE_ON_RED << 8) | 7; // •
    PanicPrint(6, y++, "Interrupts: DISABLED", COLOR_WHITE_ON_RED);
    video_memory[y * SCREEN_WIDTH + 4] = (COLOR_WHITE_ON_RED << 8) | 7; // •
    PanicPrint(6, y++, "Memory: UNKNOWN", COLOR_WHITE_ON_RED);
    video_memory[y * SCREEN_WIDTH + 4] = (COLOR_WHITE_ON_RED << 8) | 7; // •
    PanicPrint(6, y++, "Scheduler: DISABLED", COLOR_WHITE_ON_RED);
    video_memory[y * SCREEN_WIDTH + 4] = (COLOR_WHITE_ON_RED << 8) | 7; // •
    PanicPrint(6, y++, "Recovery: UNKNOWN", COLOR_WHITE_ON_RED);
    y++;


    // Bottom warning
    for (int i = 0; i < SCREEN_WIDTH; i++) {
        video_memory[22 * SCREEN_WIDTH + i] = (COLOR_BRIGHT_RED_ON_BLACK << 8) | 219; // █
        video_memory[24 * SCREEN_WIDTH + i] = (COLOR_BRIGHT_RED_ON_BLACK << 8) | 219; // █
    }
    PanicPrintCentered(23, "SYSTEM HALTED - MANUAL INTERVENTION REQUIRED", COLOR_BRIGHT_WHITE_ON_RED);
}

void BlinkingEffect() {
    // Create blinking effect for critical messages
    for (int blink = 0; blink < 5; blink++) {
        // Bright phase
        PanicPrintCentered(2, "█▓▒░ KERNEL PANIC ░▒▓█", COLOR_BRIGHT_WHITE_ON_RED);
        for (volatile int i = 0; i < 50000000; i++);

        // Dim phase
        PanicPrintCentered(2, "█▓▒░ KERNEL PANIC ░▒▓█", COLOR_BLACK_ON_RED);
        for (volatile int i = 0; i < 50000000; i++);
    }

    // Final bright state
    PanicPrintCentered(2, "█▓▒░ KERNEL PANIC ░▒▓█", COLOR_BRIGHT_WHITE_ON_RED);
}

void ForceReboot() {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;

    PanicPrint(2, 21, "INITIATING EMERGENCY REBOOT...", COLOR_BRIGHT_WHITE_ON_RED);

    // Progress bar for reboot using CP437 block characters
    for (int i = 0; i < 40; i++) {
        video_memory[21 * SCREEN_WIDTH + 35 + i] = (COLOR_YELLOW_ON_RED << 8) | 219; // █
        for (volatile int j = 0; j < 10000000; j++);
    }

    asm volatile("lidt %0" :: "m"(*(short*)0));
    asm volatile("int $0x3");
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void KernelPanicHandler(const char* message, uint64_t error_code, PanicContext* ctx) {
    ShowPanicHeader();
    BlinkingEffect();
    ShowPanicDetails(message, error_code, ctx);

    for (volatile int i = 0; i < 1000000000; i++);

    ForceReboot();
}

void __attribute__((noreturn)) Panic(const char* message) {
    asm volatile("cli");

    PanicContext ctx = {0};
    // Get basic context (simplified)
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));

    KernelPanicHandler(message, PANIC_GENERAL, &ctx);

    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");

    PanicContext ctx = {0};
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));

    KernelPanicHandler(message, error_code, &ctx);

    while (1) {
        __asm__ __volatile__("hlt");
    }
}

void __attribute__((noreturn)) PanicWithContext(const char* message, uint64_t error_code, const char* function, const char* file, int line) {
    asm volatile("cli");

    PanicContext ctx = {0};
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    ctx.error_code = error_code;
    ctx.function = function;
    ctx.file = file;
    ctx.line = line;

    KernelPanicHandler(message, error_code, &ctx);

    while (1) {
        __asm__ __volatile__("hlt");
    }
}

// Convenience macros
#define PANIC(msg) PanicWithContext(msg, PANIC_GENERAL, __FUNCTION__, __FILE__, __LINE__)
#define PANIC_CODE(msg, code) PanicWithContext(msg, code, __FUNCTION__, __FILE__, __LINE__)
#define ASSERT(condition) do { if (!(condition)) PANIC("Assertion failed: " #condition); } while(0)