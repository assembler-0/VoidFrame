#include "VBEConsole.h"

#include "../../mm/MemOps.h"
#include "Console.h"
#include "Vesa.h"
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
            vbe_console.color_buffer[row][col] = VBE_CONSOLE_DEFAULT_COLOR;  // Use 0x07, not 0x08
        }
    }
    vbe_console.cursor_x = 0;
    vbe_console.cursor_y = 0;
    VBEFillScreen(vbe_console.bg_color);
}

extern vbe_info_t vbe_info;

static void VBEConsoleScroll(void) {
    // Move text buffer (existing code is fine)
    for (int row = 0; row < CONSOLE_ROWS - 1; row++) {
        for (int col = 0; col < CONSOLE_COLS; col++) {
            vbe_console.buffer[row][col] = vbe_console.buffer[row + 1][col];
            vbe_console.color_buffer[row][col] = vbe_console.color_buffer[row + 1][col];
        }
    }

    // Clear last line in buffer
    for (int col = 0; col < CONSOLE_COLS; col++) {
        vbe_console.buffer[CONSOLE_ROWS - 1][col] = ' ';
        vbe_console.color_buffer[CONSOLE_ROWS - 1][col] = VBE_CONSOLE_DEFAULT_COLOR;
    }

    // Optimized framebuffer scroll - copy memory instead of redrawing
    uint32_t* fb = (uint32_t*)vbe_info.framebuffer;
    uint32_t line_size = vbe_info.pitch / 4;  // pitch in dwords
    uint32_t scroll_lines = CHAR_HEIGHT * line_size;
    uint32_t total_lines = (CONSOLE_ROWS - 1) * CHAR_HEIGHT * line_size;

    // Move framebuffer content up by one text line
    FastMemcpy(fb, fb + scroll_lines, total_lines * 4);

    // Clear only the last text line
    uint32_t last_line_start = (CONSOLE_ROWS - 1) * CHAR_HEIGHT * line_size;
    for (uint32_t i = 0; i < scroll_lines; i++) {
        fb[last_line_start + i] = vbe_console.bg_color;
    }
}

void VBEConsoleRefresh(void) {
    // Memory barrier before framebuffer operations
    __asm__ volatile("mfence" ::: "memory");

    VBEFillScreen(vbe_console.bg_color);

    // Memory barrier after clear
    __asm__ volatile("mfence" ::: "memory");

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

    // Final memory barrier
    __asm__ volatile("mfence" ::: "memory");
}

void VBEConsolePutChar(char c) {
    // Calculate current VGA attribute from fg/bg colors
    uint8_t current_attr = 0x07;  // Default
    // Find the palette index for current fg_color
    for (int i = 0; i < 16; i++) {
        if (vga_palette[i] == vbe_console.fg_color) {
            current_attr = (current_attr & 0xF0) | i;
            break;
        }
    }
    // Find the palette index for current bg_color
    for (int i = 0; i < 16; i++) {
        if (vga_palette[i] == vbe_console.bg_color) {
            current_attr = (current_attr & 0x0F) | (i << 4);
            break;
        }
    }

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
                vbe_console.color_buffer[vbe_console.cursor_y][vbe_console.cursor_x] = current_attr;  // ADD THIS
                VBEDrawChar(vbe_console.cursor_x * CHAR_WIDTH,
                           vbe_console.cursor_y * CHAR_HEIGHT,
                           ' ', vbe_console.fg_color, vbe_console.bg_color);
            }
            break;
        default:
            if (c >= 32 && c < 127) {
                vbe_console.buffer[vbe_console.cursor_y][vbe_console.cursor_x] = c;
                vbe_console.color_buffer[vbe_console.cursor_y][vbe_console.cursor_x] = current_attr;  // ADD THIS
                VBEDrawChar(vbe_console.cursor_x * CHAR_WIDTH,
                           vbe_console.cursor_y * CHAR_HEIGHT,
                           c, vbe_console.fg_color, vbe_console.bg_color);
                vbe_console.cursor_x++;
                if (vbe_console.cursor_x >= CONSOLE_COLS) {
                    vbe_console.cursor_x = 0;
                    vbe_console.cursor_y++;
                }
            }
            break;
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