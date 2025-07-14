#include "Splash.h"
#include "stdbool.h"
#include "Kernel.h"
#include "stdint.h"

#define VIDEO_MEMORY (0xB8000)
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25

void DrawBox(int x, int y, int width, int height) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t color = (0x0F << 8);

    // Corners
    video_memory[y * SCREEN_WIDTH + x] = color | 201; // Top-left
    video_memory[y * SCREEN_WIDTH + x + width - 1] = color | 187; // Top-right
    video_memory[(y + height - 1) * SCREEN_WIDTH + x] = color | 200; // Bottom-left
    video_memory[(y + height - 1) * SCREEN_WIDTH + x + width - 1] = color | 188; // Bottom-right

    // Horizontal lines
    for (int i = 1; i < width - 1; i++) {
        video_memory[y * SCREEN_WIDTH + x + i] = color | 205;
        video_memory[(y + height - 1) * SCREEN_WIDTH + x + i] = color | 205;
    }

    // Vertical lines
    for (int i = 1; i < height - 1; i++) {
        video_memory[(y + i) * SCREEN_WIDTH + x] = color | 186;
        video_memory[(y + i) * SCREEN_WIDTH + x + width - 1] = color | 186;
    }
}

void PrintString(int x, int y, const char* str) {
    uint16_t* video_memory = (uint16_t*)VIDEO_MEMORY;
    uint16_t color = (0x0F << 8);
    int i = 0;
    while (str[i] != '\0') {
        video_memory[y * SCREEN_WIDTH + x + i] = color | str[i];
        i++;
    }
}

void ShowSplashScreen() {
    ClearScreen();
    DrawBox(10, 5, 60, 15);
    PrintString(28, 7, "MKernel - v0.0.1-beta");
    PrintString(24, 9, "A lightweight 64-bit kernel");
    PrintString(25, 11, "Copyright (c) 2025, Atheria");
    for (volatile int i = 0; i < 3000000000; i ++);
}