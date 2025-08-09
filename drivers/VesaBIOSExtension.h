#ifndef VOIDFRAME_VESABIOSEXTENSION_H
#define VOIDFRAME_VESABIOSEXTENSION_H

#include "stdint.h"

// Common colors (32-bit RGBA format)
#define VBE_COLOR_BLACK     0x00000000
#define VBE_COLOR_WHITE     0x00FFFFFF
#define VBE_COLOR_RED       0x00FF0000
#define VBE_COLOR_GREEN     0x0000FF00
#define VBE_COLOR_BLUE      0x000000FF
#define VBE_COLOR_YELLOW    0x00FFFF00
#define VBE_COLOR_CYAN      0x0000FFFF
#define VBE_COLOR_MAGENTA   0x00FF00FF
#define VBE_COLOR_GRAY      0x00808080
#define VBE_COLOR_DARK_GRAY 0x00404040

// VBE info structure
typedef struct {
    volatile uint32_t *framebuffer;  // Pointer to framebuffer
    uint32_t width;         // Screen width
    uint32_t height;        // Screen height
    uint32_t pitch;         // Bytes per scanline
    uint32_t bpp;           // Bits per pixel

    // NEW: Add fields for the color layout
    uint8_t red_mask_size;
    uint8_t red_field_position;
    uint8_t green_mask_size;
    uint8_t green_field_position;
    uint8_t blue_mask_size;
    uint8_t blue_field_position;
} vbe_info_t;

// VBE functions
int VBEInit(uint32_t multiboot_info_addr);
void VBEPutPixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t VBEGetPixel(uint32_t x, uint32_t y);
void VBEFillScreen(uint32_t color);
void VBEDrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
void VBEDrawLine(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color);
void VBEDrawChar(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color);
void VBEDrawString(uint32_t x, uint32_t y, const char* str, uint32_t fg_color, uint32_t bg_color);
void VBEShowSplash(void);
// Utility functions
vbe_info_t* VBEGetInfo(void);
int VBEIsInitialized(void);

#endif // VOIDFRAME_VESABIOSEXTENSION_H
