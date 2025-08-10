#include "VBEConsole.h"
#include "VesaBIOSExtension.h"
#include "stdint.h"

// Full screen console using 800x600
#define SCREEN_WIDTH   800
#define SCREEN_HEIGHT  600
#define CHAR_WIDTH     8
#define CHAR_HEIGHT    16
#define CONSOLE_COLS   (SCREEN_WIDTH / CHAR_WIDTH)    // 100 columns
#define CONSOLE_ROWS   (SCREEN_HEIGHT / CHAR_HEIGHT)  // 75 rows

// Console state
static struct {
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t fg_color;
    uint32_t bg_color;
    uint8_t cursor_visible;
    char buffer[CONSOLE_ROWS][CONSOLE_COLS];
    uint8_t color_buffer[CONSOLE_ROWS][CONSOLE_COLS];
} vbe_console = {
    .cursor_x = 0,
    .cursor_y = 0,
    .fg_color = 0xFFFFFF,
    .bg_color = 0x000000,
    .cursor_visible = 1
};

// VGA color palette for compatibility
static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

void VBEConsoleInit(void) {
    VBEConsoleClear();
}

void VBEConsoleClear(void) {
    for (int row = 0; row < CONSOLE_ROWS; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
            vbe_console.buffer[row][col] = ' ';
            vbe_console.color_buffer[row][col] = 0x08; // Gray on black
        }
    }
    vbe_console.cursor_x = 0;
    vbe_console.cursor_y = 0;
    VBEFillScreen(vbe_console.bg_color);
}

static void VBEConsoleScroll(void) {
    // Move all lines up
    for (int row = 0; row < CONSOLE_ROWS - 1; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
            vbe_console.buffer[row][col] = vbe_console.buffer[row + 1][col];
            vbe_console.color_buffer[row][col] = vbe_console.color_buffer[row + 1][col];
        }
    }

    // Clear last line
    for (int col = 0; col < CONSOLE_COLS; col++) {
        vbe_console.buffer[CONSOLE_ROWS - 1][col] = ' ';
        vbe_console.color_buffer[CONSOLE_ROWS - 1][col] = 0x08;
    }

    // Redraw screen
    VBEConsoleRefresh();
}

void VBEConsoleRefresh(void) {
    VBEFillScreen(vbe_console.bg_color);

    for (int row = 0; row < CONSOLE_ROWS; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
            char c = vbe_console.buffer[row][col];
            if (c && c != ' ') {
                uint8_t attr = vbe_console.color_buffer[row][col];
                uint32_t fg = vga_palette[attr & 0x0F];
                uint32_t bg = (attr >> 4) ? vga_palette[(attr >> 4) & 0x0F] : 0;
                VBEDrawChar(col * CHAR_WIDTH, row * CHAR_HEIGHT, c, fg, bg);
            }
        }
    }
}

void VBEConsolePutChar(char c) {
    switch (c) {
        case '\n':
            vbe_console.cursor_x = 0;
            vbe_console.cursor_y++;
            break;
        case '\r':
            vbe_console.cursor_x = 0;
            break;
        case '\t':
            vbe_console.cursor_x = ((vbe_console.cursor_x + 8) / 8) * 8;
            if (vbe_console.cursor_x >= CONSOLE_COLS) {
                vbe_console.cursor_x = 0;
                vbe_console.cursor_y++;
            }
            break;
        case '\b':
            if (vbe_console.cursor_x > 0) {
                vbe_console.cursor_x--;
                vbe_console.buffer[vbe_console.cursor_y][vbe_console.cursor_x] = ' ';
                VBEDrawChar(vbe_console.cursor_x * CHAR_WIDTH,
                           vbe_console.cursor_y * CHAR_HEIGHT,
                           ' ', vbe_console.fg_color, vbe_console.bg_color);
            }
            break;
        default:
            if (c >= 32 && c < 127) {
                vbe_console.buffer[vbe_console.cursor_y][vbe_console.cursor_x] = c;
                VBEDrawChar(vbe_console.cursor_x * CHAR_WIDTH,
                           vbe_console.cursor_y * CHAR_HEIGHT,
                           c, vbe_console.fg_color, vbe_console.bg_color);
                vbe_console.cursor_x++;
            }
            break;
    }

    if (vbe_console.cursor_x >= CONSOLE_COLS) {
        vbe_console.cursor_x = 0;
        vbe_console.cursor_y++;
    }

    if (vbe_console.cursor_y >= CONSOLE_ROWS) {
        VBEConsoleScroll();
        vbe_console.cursor_y = CONSOLE_ROWS - 1;
    }
}

void VBEConsolePrint(const char* str) {
    while (*str) {
        VBEConsolePutChar(*str++);
    }
}

void VBEConsoleSetColor(uint8_t color) {
    vbe_console.fg_color = vga_palette[color & 0x0F];
    vbe_console.bg_color = vga_palette[(color >> 4) & 0x0F];
}

void VBEConsoleSetCursor(uint32_t x, uint32_t y) {
    if (x < CONSOLE_COLS) vbe_console.cursor_x = x;
    if (y < CONSOLE_ROWS) vbe_console.cursor_y = y;
}