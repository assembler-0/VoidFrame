#ifndef MB2_H
#define MB2_H

#include "stdint.h"

#define MULTIBOOT2_MAGIC_HEADER        0xE85250D6
#define MULTIBOOT2_BOOTLOADER_MAGIC    0x36D76289
#define MULTIBOOT2_TAG_TYPE_END        0
#define MULTIBOOT2_TAG_TYPE_CMDLINE    1
#define MULTIBOOT2_TAG_TYPE_BOOTLOADER_NAME 2
#define MULTIBOOT2_TAG_TYPE_MODULE     3
#define MULTIBOOT2_TAG_TYPE_MMAP       6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT2_MEMORY_AVAILABLE    1
#define MULTIBOOT_TAG_FRAMEBUFFER 8
#define MULTIBOOT_TAG_VBE         7

struct MultibootTag {
    uint32_t type;
    uint32_t size;
};

struct MultibootTagMmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
};

struct MultibootMmapEntry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
};

typedef struct {
    struct MultibootTag tag;
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
} MultibootTagFramebuffer;

struct MultibootModuleTag {
    struct MultibootTag tag;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
};

#endif