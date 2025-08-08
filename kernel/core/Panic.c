#include "Panic.h"

#include "Serial.h"
#include "stdint.h"

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define VIDEO_MEMORY (0xB8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

// --- Color Scheme (Unchanged) ---
#define COLOR_RED_ON_BLACK 0x0C
#define COLOR_BRIGHT_RED_ON_BLACK 0x04
#define COLOR_WHITE_ON_RED 0x4F
#define COLOR_YELLOW_ON_RED 0x4E
#define COLOR_BLACK_ON_RED 0x40
#define COLOR_BRIGHT_WHITE_ON_RED 0x4F
#define COLOR_FLASHING_RED 0x8C // Note: Flashing can be distracting, used sparingly.

// --- Panic Error Codes (Unchanged) ---


// --- Panic Context Structure (Unchanged) ---
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

// --- Low-Level Drawing Primitives ---

void RedScreen() {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t red_attr = (COLOR_BLACK_ON_RED << 8); // Black on Red is less jarring for the background

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        video_memory[i] = red_attr | ' ';
    }
}

void PanicPrint(int x, int y, const char* str, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)(VIDEO_MEMORY + (y * SCREEN_WIDTH + x) * 2);
    uint16_t attr = (color << 8);

    while (*str) {
        *video_memory++ = attr | *str++;
    }
}

void PanicPrintCentered(int y, const char* str, uint8_t color) {
    int len = 0;
    while (str[len] != '\0') len++;
    int x = (SCREEN_WIDTH - len) / 2;
    if (x < 0) x = 0;
    PanicPrint(x, y, str, color);
}

// NEW: Helper to print decimal numbers (for line numbers)
void PanicPrintDec(int x, int y, uint32_t value, uint8_t color) {
    char buffer[11]; // Max 10 digits for a 32-bit unsigned int + null terminator
    char* p = buffer + sizeof(buffer) - 1;
    *p = '\0';

    if (value == 0) {
        *--p = '0';
    } else {
        while (value > 0) {
            *--p = '0' + (value % 10);
            value /= 10;
        }
    }
    PanicPrint(x, y, p, color);
}

void PanicPrintHex(int x, int y, uint64_t value, uint8_t color) {
    const char hex[] = "0123456789ABCDEF";
    char buffer[19] = "0x";

    // Position the pointer to the end of the buffer part for hex digits
    char* p = buffer + 2;

    for (int i = 15; i >= 0; i--) {
        p[15-i] = hex[(value >> (i * 4)) & 0xF];
    }
    p[16] = '\0';

    PanicPrint(x, y, buffer, color);
}

void DrawPanicBox(int x, int y, int width, int height, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);

    // Corners using double-line characters
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


void ShowShutdownSequence(int y_start) {
    const int num_steps = 4;

    for (int i = 0; i < num_steps; i++) {
        const int bar_x = 38;
        const char* steps[4] = {
            "Disabling services...",
            "Unloading modules...",
            "Unmounting filesystems...",
            "Scanning..."
        };
        const int current_y = y_start + i;

        // Print the current step's text
        PanicPrint(4, current_y, steps[i], COLOR_WHITE_ON_RED);

        // Don't draw a progress bar for the final "halt reached" message.
        if (i < num_steps) {
            const int bar_width = 28;
            // Draw the progress bar
            for (int j = 0; j < bar_width; j++) {
                PanicPrint(bar_x + j, current_y, "█", COLOR_YELLOW_ON_RED);
                // Smaller delay for a smooth bar animation
                for (volatile int d = 0; d < 4000000; d++);
            }
            // Print the status when done
            PanicPrint(bar_x + bar_width + 2, current_y, "[ DONE ]", COLOR_BRIGHT_WHITE_ON_RED);
        }
    }
}

// --- High-Level Panic Screen Composition ---

void KernelPanicHandler(const char* message, uint64_t error_code, PanicContext* ctx) {
    RedScreen();

    // --- Main Title and Blinking Effect ---
    PanicPrintCentered(2, "  !! KERNEL PANIC !!  ", COLOR_BRIGHT_WHITE_ON_RED);
    for (volatile int i = 0; i < 90000000; i++); // Short delay
    PanicPrintCentered(2, "  !! KERNEL PANIC !!  ", COLOR_YELLOW_ON_RED);
    for (volatile int i = 0; i < 90000000; i++); // Short delay
    PanicPrintCentered(2, "  !! KERNEL PANIC !!  ", COLOR_BRIGHT_WHITE_ON_RED);

    // --- Box 1: System Halted Reason ---
    DrawPanicBox(2, 4, 76, 4, COLOR_YELLOW_ON_RED);
    PanicPrint(4, 5, "[!] Your system has been halted due to an unrecoverable error.", COLOR_WHITE_ON_RED);
    PanicPrint(4, 6, "[!] ERROR: ", COLOR_BRIGHT_WHITE_ON_RED);
    PanicPrint(15, 6, message, COLOR_WHITE_ON_RED);

    // --- Box 2: CPU Context ---
    DrawPanicBox(2, 9, 37, 8, COLOR_YELLOW_ON_RED);
    PanicPrint(4, 10, "[i] CPU CONTEXT", COLOR_BRIGHT_WHITE_ON_RED);
    if (ctx) {
        PanicPrint(4, 12, "RIP:", COLOR_WHITE_ON_RED); PanicPrintHex(9, 12, ctx->rip, COLOR_YELLOW_ON_RED);
        PanicPrint(4, 13, "RSP:", COLOR_WHITE_ON_RED); PanicPrintHex(9, 13, ctx->rsp, COLOR_WHITE_ON_RED);
        PanicPrint(4, 14, "RBP:", COLOR_WHITE_ON_RED); PanicPrintHex(9, 14, ctx->rbp, COLOR_WHITE_ON_RED);
        PanicPrint(4, 15, "CODE:",COLOR_WHITE_ON_RED); PanicPrintHex(9, 15, error_code, COLOR_WHITE_ON_RED);
    }

    // --- Box 3: Source Location ---
    DrawPanicBox(41, 9, 37, 8, COLOR_YELLOW_ON_RED);
    PanicPrint(43, 10, "[i] SOURCE LOCATION", COLOR_BRIGHT_WHITE_ON_RED);
    if (ctx && ctx->file) {
        PanicPrint(43, 12, "FILE:", COLOR_WHITE_ON_RED); PanicPrint(50, 12, ctx->file, COLOR_WHITE_ON_RED);
        PanicPrint(43, 13, "FUNC:", COLOR_WHITE_ON_RED); PanicPrint(50, 13, ctx->function, COLOR_WHITE_ON_RED);
        PanicPrint(43, 14, "LINE:", COLOR_WHITE_ON_RED); PanicPrintDec(50, 14, ctx->line, COLOR_WHITE_ON_RED);
    } else {
        PanicPrint(43, 12, "Unavailable", COLOR_WHITE_ON_RED);
    }
    for (volatile int i = 0; i < 90000000; i++);

    DrawPanicBox(2, 18, 76, 6, COLOR_YELLOW_ON_RED);
    // IMPORTANT: We are *simulating* these steps for visual effect and safety.
    ShowShutdownSequence(19);

    PanicPrintCentered(23, "SYSTEM HALTED", COLOR_BRIGHT_WHITE_ON_RED);

    SerialWrite("\n[FATAl] - [KERNEL PANIC] -- [not syncing - General Protection Fault] -- EXPERIMENTAL\n");
    // Standard practice is to halt indefinitely, allowing the user to read the screen.
    // Forcing a reboot might lose valuable diagnostic info.
    while (1) {
        __asm__ __volatile__("hlt");
    }
}


// --- Public Panic Interface Functions ---

// Helper to get the instruction pointer of the caller.
static inline uint64_t __get_rip(void) {
    return (uint64_t)__builtin_return_address(0);
}



void __attribute__((noreturn)) Panic(const char* message) {
    asm volatile("cli");
    PanicContext ctx = {0};

    ctx.rip = __get_rip();
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));

    KernelPanicHandler(message, PANIC_GENERAL, &ctx);
}

void __attribute__((noreturn)) PanicWithCode(const char* message, uint64_t error_code) {
    asm volatile("cli");
    PanicContext ctx = {0};

    ctx.rip = __get_rip();
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));

    KernelPanicHandler(message, error_code, &ctx);
}

void __attribute__((noreturn)) PanicWithContext(const char* message, uint64_t error_code, const char* function, const char* file, int line) {
    asm volatile("cli");
    PanicContext ctx = {0};

    // RIP is the address of the instruction AFTER the call to this function.
    // In a PANIC macro, this is exactly where the error was detected.
    ctx.rip = __get_rip();
    asm volatile("movq %%rsp, %0" : "=r"(ctx.rsp));
    asm volatile("movq %%rbp, %0" : "=r"(ctx.rbp));
    ctx.error_code = error_code;
    ctx.function = function;
    ctx.file = file;
    ctx.line = line;

    KernelPanicHandler(message, error_code, &ctx);
    while(1);
}