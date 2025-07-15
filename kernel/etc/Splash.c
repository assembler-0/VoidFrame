#include "Splash.h"
#include "stdbool.h"
#include "Kernel.h"
#include "stdint.h"

#define VIDEO_MEMORY (0xB8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define DATE __DATE__
#define TIME __TIME__
// Color definitions for better readability
#define COLOR_WHITE_ON_BLACK 0x0F
#define COLOR_CYAN_ON_BLACK 0x0B
#define COLOR_YELLOW_ON_BLACK 0x0E
#define COLOR_GREEN_ON_BLACK 0x0A
#define COLOR_RED_ON_BLACK 0x0C
#define COLOR_BLUE_ON_BLACK 0x09

void DrawBox(int x, int y, int width, int height, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);

    // Double-line box characters for a more professional look
    video_memory[y * SCREEN_WIDTH + x] = attr | 201; // ╔ Top-left
    video_memory[y * SCREEN_WIDTH + x + width - 1] = attr | 187; // ╗ Top-right
    video_memory[(y + height - 1) * SCREEN_WIDTH + x] = attr | 200; // ╚ Bottom-left
    video_memory[(y + height - 1) * SCREEN_WIDTH + x + width - 1] = attr | 188; // ╝ Bottom-right

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

void PrintString(int x, int y, const char* str, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);
    int i = 0;
    while (str[i] != '\0') {
        video_memory[y * SCREEN_WIDTH + x + i] = attr | str[i];
        i++;
    }
}

void PrintCentered(int y, const char* str, uint8_t color) {
    int len = 0;
    while (str[len] != '\0') len++; // Get string length
    int x = (SCREEN_WIDTH - len) / 2;
    PrintString(x, y, str, color);
}

void DrawProgressBar(int x, int y, int width, int progress, uint8_t color) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t attr = (color << 8);

    // Draw progress bar frame
    video_memory[y * SCREEN_WIDTH + x] = attr | '[';
    video_memory[y * SCREEN_WIDTH + x + width - 1] = attr | ']';

    // Fill progress
    int filled = (progress * (width - 2)) / 100;
    for (int i = 1; i < width - 1; i++) {
        if (i <= filled) {
            video_memory[y * SCREEN_WIDTH + x + i] = attr | 219; // █ Full block
        } else {
            video_memory[y * SCREEN_WIDTH + x + i] = attr | 176; // ░ Light shade
        }
    }
}

void ShowSplashScreen() {
    ClearScreen();

    // Main border
    DrawBox(5, 2, 70, 21, COLOR_CYAN_ON_BLACK);
    ClearScreen();
    // Title section
    DrawBox(10, 5, 60, 12, COLOR_WHITE_ON_BLACK);
    PrintCentered(9, "VoidFrame", COLOR_CYAN_ON_BLACK);
    PrintCentered(10, "Version 0.0.1-beta", COLOR_CYAN_ON_BLACK);
    PrintString(23,11, "Compiled on ", COLOR_CYAN_ON_BLACK);
    PrintString(35,11, TIME, COLOR_CYAN_ON_BLACK);
    PrintString(44,11, DATE, COLOR_CYAN_ON_BLACK);
    PrintCentered(12, "Copyright (c) 2025, Atheria", COLOR_CYAN_ON_BLACK);
    // System info
    PrintString(12, 17, "System Status:", COLOR_CYAN_ON_BLACK);
    PrintString(16, 18, "Initializing...", COLOR_CYAN_ON_BLACK);

    // Loading progress bar
    PrintString(12, 20, "Loading: ", COLOR_CYAN_ON_BLACK);

    // Simulate loading progress
    for (int progress = 0; progress <= 100; progress += 7) {
        DrawProgressBar(21, 20, 48, progress, COLOR_CYAN_ON_BLACK);

        // Simple delay
        for (volatile int i = 0; i < 50000000; i++);
    }

    // Final delay
    for (volatile int i = 0; i < 70000000; i++);
}