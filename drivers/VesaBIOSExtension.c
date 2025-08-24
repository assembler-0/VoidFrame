#include "VesaBIOSExtension.h"
#include "Font.h"
#include "MemOps.h"
#include "Serial.h"
#include "stdint.h"
#include "stdlib.h"

extern const uint32_t _binary_splash1_32_raw_start[];
extern const uint32_t _binary_panic_32_raw_start[];

// Update your array of pointers
const uint32_t* splash_images[] = {
    _binary_splash1_32_raw_start
};

const unsigned int num_splash_images = sizeof(splash_images) / sizeof(uint32_t*);

const uint32_t* panic_images[] = {
    _binary_panic_32_raw_start
};

const unsigned int num_panic_images = sizeof(panic_images) / sizeof(uint32_t*);

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
    uint16_t  reserved;
    uint8_t  framebuffer_red_field_position;
    uint8_t  framebuffer_red_mask_size;
    uint8_t  framebuffer_green_field_position;
    uint8_t  framebuffer_green_mask_size;
    uint8_t  framebuffer_blue_field_position;
    uint8_t  framebuffer_blue_mask_size;
} multiboot_tag_framebuffer_t;

vbe_info_t vbe_info = {0};
static int vbe_initialized = 0;

void delay(uint32_t count) {
    while (count--) {
        __asm__ volatile("nop");
    }
}

int VBEInit(uint32_t multiboot_info_addr) {
    SerialWrite("[VESA]: Parsing Multiboot2 info...\n");
    uint8_t *tag_ptr = (uint8_t*)(multiboot_info_addr + 8);

    while (1) {
        multiboot_tag_t *tag = (multiboot_tag_t*)tag_ptr;
        if (tag->type == 0) break; // End tag

        if (tag->type == MULTIBOOT_TAG_FRAMEBUFFER) {
            multiboot_tag_framebuffer_t *fb_tag = (multiboot_tag_framebuffer_t*)tag;

            // --- Store all the info ---
            vbe_info.framebuffer = (volatile uint32_t*)(uintptr_t)fb_tag->framebuffer_addr;
            vbe_info.width = fb_tag->framebuffer_width;
            vbe_info.height = fb_tag->framebuffer_height;
            vbe_info.bpp = fb_tag->framebuffer_bpp;
            vbe_info.pitch = fb_tag->framebuffer_pitch;

            // NEW: Store the color info
            if (fb_tag->framebuffer_type == 1) { // RGB
                vbe_info.red_mask_size = fb_tag->framebuffer_red_mask_size;
                vbe_info.red_field_position = fb_tag->framebuffer_red_field_position;
                vbe_info.green_mask_size = fb_tag->framebuffer_green_mask_size;
                vbe_info.green_field_position = fb_tag->framebuffer_green_field_position;
                vbe_info.blue_mask_size = fb_tag->framebuffer_blue_mask_size;
                vbe_info.blue_field_position = fb_tag->framebuffer_blue_field_position;
            } else {
                SerialWrite("ERROR: Unsupported framebuffer type (expected RGB)\n");
                vbe_initialized = 0;
                return -1;
            }

            // --- Add detailed logging ---
            SerialWrite("[VESA]: Framebuffer Found!\n");
            SerialWrite("  Resolution: ");
            SerialWriteDec(vbe_info.width); SerialWrite("x");
            SerialWriteDec(vbe_info.height); SerialWrite("x");
            SerialWriteDec(vbe_info.bpp); SerialWrite("\n");
            SerialWrite("  Red Mask: size="); SerialWriteDec(vbe_info.red_mask_size);
            SerialWrite(", pos="); SerialWriteDec(vbe_info.red_field_position); SerialWrite("\n");
            SerialWrite("  Green Mask: size="); SerialWriteDec(vbe_info.green_mask_size);
            SerialWrite(", pos="); SerialWriteDec(vbe_info.green_field_position); SerialWrite("\n");
            SerialWrite("  Blue Mask: size="); SerialWriteDec(vbe_info.blue_mask_size);
            SerialWrite(", pos="); SerialWriteDec(vbe_info.blue_field_position); SerialWrite("\n");


            // Important check!
            if (vbe_info.bpp != 32) {
                SerialWrite("ERROR: Unsupported BPP, this code only handles 32-bpp!\n");
                vbe_initialized = 0; // Mark as not initialized
                return -1;
            }

            vbe_initialized = 1;
            return 0;
        }
        tag_ptr += ((tag->size + 7) & ~7);
    }
    SerialWrite("[VESA]: No framebuffer tag found in Multiboot info\n");
    return -1;
}

static uint32_t VBEMapColor(uint32_t hex_color) {
    // Extract the R, G, B components from the standard 0xRRGGBB format
    uint8_t r8 = (hex_color >> 16) & 0xFF;
    uint8_t g8 = (hex_color >> 8) & 0xFF;
    uint8_t b8 = hex_color & 0xFF;

    // Scale down to mask sizes (assumes mask_size in [1..8])
    uint32_t r = (vbe_info.red_mask_size   >= 8) ? r8 : (r8 >> (8 - vbe_info.red_mask_size));
    uint32_t g = (vbe_info.green_mask_size >= 8) ? g8 : (g8 >> (8 - vbe_info.green_mask_size));
    uint32_t b = (vbe_info.blue_mask_size  >= 8) ? b8 : (b8 >> (8 - vbe_info.blue_mask_size));

    // Mask to exact width and shift into place
    uint32_t rmask = (vbe_info.red_mask_size   >= 32) ? 0xFFFFFFFFu : ((1u << vbe_info.red_mask_size)   - 1u);
    uint32_t gmask = (vbe_info.green_mask_size >= 32) ? 0xFFFFFFFFu : ((1u << vbe_info.green_mask_size) - 1u);
    uint32_t bmask = (vbe_info.blue_mask_size  >= 32) ? 0xFFFFFFFFu : ((1u << vbe_info.blue_mask_size)  - 1u);

    uint32_t mapped_color = ((r & rmask) << vbe_info.red_field_position)   |
    ((g & gmask) << vbe_info.green_field_position) |
    ((b & bmask) << vbe_info.blue_field_position);

    return mapped_color;
}

void VBEPutPixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!vbe_initialized || x >= vbe_info.width || y >= vbe_info.height) {
        return;
    }

    // Convert the standard color to the hardware-specific format
    uint32_t mapped_color = VBEMapColor(color);

    uint32_t offset = y * (vbe_info.pitch / 4) + x;
    vbe_info.framebuffer[offset] = mapped_color;
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

    if ((unsigned char)c >= 256) {
        return; // Character out of bounds
    }

    const unsigned char* glyph = console_font[(unsigned char)c];

    // Use the font dimensions from font.h
    for (int row = 0; row < FONT_HEIGHT; row++) {
        // Calculate which byte contains this row's data
        int byte_index = row * ((FONT_WIDTH + 7) / 8); // Number of bytes per row

        for (int col = 0; col < FONT_WIDTH; col++) {
            int bit_position = 7 - (col % 8); // MSB first

            if ((glyph[byte_index + (col / 8)] >> bit_position) & 1) {
                VBEPutPixel(x + col, y + row, fg_color);
            } else {
                // Always draw background to ensure proper clearing
                VBEPutPixel(x + col, y + row, bg_color);
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
            current_y += FONT_HEIGHT; // Use actual font height
        } else if (str[i] == '\r') {
            current_x = x;
        } else if (str[i] == '\t') {
            // Tab = 4 spaces
            current_x += FONT_WIDTH * 4;
        } else {
            // Check bounds before drawing
            if (current_x + FONT_WIDTH <= vbe_info.width &&
                current_y + FONT_HEIGHT <= vbe_info.height) {
                VBEDrawChar(current_x, current_y, str[i], fg_color, bg_color);
                }
            current_x += FONT_WIDTH;
        }

        // Handle line wrapping
        if (current_x + FONT_WIDTH > vbe_info.width) {
            current_x = x;
            current_y += FONT_HEIGHT;
        }
    }
}

void VBEGetTextDimensions(const char* str, uint32_t* width, uint32_t* height) {
    if (!str || !width || !height) return;

    uint32_t max_width = 0;
    uint32_t current_width = 0;
    uint32_t lines = 1;

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            if (current_width > max_width) {
                max_width = current_width;
            }
            current_width = 0;
            lines++;
        } else if (str[i] == '\t') {
            current_width += FONT_WIDTH * 4;
        } else if (str[i] != '\r') {
            current_width += FONT_WIDTH;
        }
    }

    if (current_width > max_width) {
        max_width = current_width;
    }

    *width = max_width;
    *height = lines * FONT_HEIGHT;
}

void VBEDrawStringCentered(uint32_t center_x, uint32_t center_y, const char* str,
                          uint32_t fg_color, uint32_t bg_color) {
    if (!vbe_initialized || !str) return;

    uint32_t text_width, text_height;
    VBEGetTextDimensions(str, &text_width, &text_height);

    uint32_t start_x = (center_x >= text_width / 2) ? center_x - text_width / 2 : 0;
    uint32_t start_y = (center_y >= text_height / 2) ? center_y - text_height / 2 : 0;

    VBEDrawString(start_x, start_y, str, fg_color, bg_color);
}

// Create a simple splash screen
void VBEShowSplash(void) {
    if (!vbe_initialized) return;

    for (unsigned int i = 0; i < num_splash_images; i++) { // Loop
        const uint32_t* image_data = (const uint32_t*)splash_images[i % num_splash_images];

        for (uint32_t y = 0; y < vbe_info.height; y++) {
            for (uint32_t x = 0; x < vbe_info.width; x++) {
                VBEPutPixel(x, y, image_data[y * vbe_info.width + x]);
            }
        }
    }
    delay(500000000);
}

void VBEShowPanic(void) {
    if (!vbe_initialized) return;
    const uint32_t* image_data = (const uint32_t*)panic_images[0];
    uint8_t* fb = (uint8_t*)vbe_info.framebuffer;

    // Copy entire scanlines at once instead of pixel-by-pixel
    for (uint32_t y = 0; y < vbe_info.height; y++) {
        // Calculate source and destination for this scanline
        const uint32_t* src = &image_data[y * vbe_info.width];
        uint8_t* dst = fb + (y * vbe_info.pitch);

        // Copy entire scanline (width * 4 bytes) using optimized memcpy
        FastMemcpy(dst, src, vbe_info.width * 4);
    }
}


vbe_info_t* VBEGetInfo(void) {
    return vbe_initialized ? &vbe_info : NULL;
}

int VBEIsInitialized(void) {
    return vbe_initialized;
}
