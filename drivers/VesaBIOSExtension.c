#include "VesaBIOSExtension.h"

#include "Io.h"
#include "Serial.h"
#include "stdint.h"
#include "stdlib.h"
#include "Font.h"

// Multiboot2 tag types
#define MULTIBOOT_TAG_FRAMEBUFFER 8
#define MULTIBOOT_TAG_VBE         7

typedef struct {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct {
    multiboot_tag_t tag;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  reserved;
    // Color info follows...
} multiboot_tag_framebuffer_t;

static vbe_info_t vbe_info = {0};
static int vbe_initialized = 0;

int VBEInit(uint32_t multiboot_info_addr) {
    SerialWrite("VBE: Parsing Multiboot2 info...\n");

    // Skip the total size field (first 8 bytes)
    uint8_t *tag_ptr = (uint8_t*)(multiboot_info_addr + 8);

    while (1) {
        multiboot_tag_t *tag = (multiboot_tag_t*)tag_ptr;

        if (tag->type == 0) break; // End tag

        if (tag->type == MULTIBOOT_TAG_FRAMEBUFFER) {
            multiboot_tag_framebuffer_t *fb_tag = (multiboot_tag_framebuffer_t*)tag;

            vbe_info.framebuffer = (uint32_t*)fb_tag->framebuffer_addr;
            vbe_info.width = fb_tag->framebuffer_width;
            vbe_info.height = fb_tag->framebuffer_height;
            vbe_info.bpp = fb_tag->framebuffer_bpp;
            vbe_info.pitch = fb_tag->framebuffer_pitch;

            SerialWrite("VBE: Found framebuffer!\n");
            SerialWrite("  Address: 0x");
            SerialWriteHex(fb_tag->framebuffer_addr);
            SerialWrite("\n  Resolution: ");
            SerialWriteDec(vbe_info.width);
            SerialWrite("x");
            SerialWriteDec(vbe_info.height);
            SerialWrite("x");
            SerialWriteDec(vbe_info.bpp);
            SerialWrite("\n  Pitch: ");
            SerialWriteDec(vbe_info.pitch);
            SerialWrite("\n");

            vbe_initialized = 1;
            return 0;
        }

        // Move to next tag (align to 8 bytes)
        tag_ptr += ((tag->size + 7) & ~7);
    }

    SerialWrite("VBE: No framebuffer found in Multiboot info\n");
    return -1;
}

void VBEPutPixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!vbe_initialized || x >= vbe_info.width || y >= vbe_info.height) {
        return;
    }

    uint32_t offset = y * (vbe_info.pitch / 4) + x;
    vbe_info.framebuffer[offset] = color;
}

uint32_t VBEGetPixel(uint32_t x, uint32_t y) {
    if (!vbe_initialized || x >= vbe_info.width || y >= vbe_info.height) {
        return 0;
    }

    uint32_t offset = y * (vbe_info.pitch / 4) + x;
    return vbe_info.framebuffer[offset];
}

void VBEFillScreen(uint32_t color) {
    if (!vbe_initialized) return;

    for (uint32_t y = 0; y < vbe_info.height; y++) {
        for (uint32_t x = 0; x < vbe_info.width; x++) {
            VBEPutPixel(x, y, color);
        }
    }
}

void VBEDrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color) {
    if (!vbe_initialized) return;

    for (uint32_t row = y; row < y + height && row < vbe_info.height; row++) {
        for (uint32_t col = x; col < x + width && col < vbe_info.width; col++) {
            VBEPutPixel(col, row, color);
        }
    }
}

void VBEDrawLine(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color) {
    if (!vbe_initialized) return;

    int dx = ABSi((int)x1 - (int)x0);
    int dy = ABSi((int)y1 - (int)y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        VBEPutPixel(x0, y0, color);

        if (x0 == x1 && y0 == y1) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void VBEDrawChar(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color) {
    if (!vbe_initialized) return;

    if ((unsigned char)c >= 128) {
        return; // Character out of bounds
    }

    const unsigned char* glyph = font8x8_basic[(unsigned char)c];

    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            if ((glyph[i] >> j) & 1) {
                VBEPutPixel(x + j, y + i, fg_color);
            } else if (bg_color != 0) { // Allows for transparent background
                VBEPutPixel(x + j, y + i, bg_color);
            }
        }
    }
}

void VBEDrawString(uint32_t x, uint32_t y, const char* str, uint32_t fg_color, uint32_t bg_color) {
    if (!vbe_initialized) return;

    uint32_t current_x = x;
    uint32_t current_y = y;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            current_x = x;
            current_y += 8;
        } else {
            VBEDrawChar(current_x, current_y, str[i], fg_color, bg_color);
            current_x += 8;
        }
    }
}


// Create a simple splash screen
void VBEShowSplash(void) {
    if (!vbe_initialized) return;

    // Black background
    VBEFillScreen(VBE_COLOR_BLACK);

    VBEDrawString(100, 20, "VoidFrame Kernel - Graphics Mode Active!",
                  VBE_COLOR_WHITE, VBE_COLOR_BLACK);

}

vbe_info_t* VBEGetInfo(void) {
    return vbe_initialized ? &vbe_info : NULL;
}

int VBEIsInitialized(void) {
    return vbe_initialized;
}