#ifndef MB2_H
#define MB2_H
#include "stdint.h"
#define MULTIBOOT2_MAGIC_HEADER        0xE85250D6
#define MULTIBOOT2_BOOTLOADER_MAGIC    0x36D76289
#define MULTIBOOT2_TAG_TYPE_END        0
#define MULTIBOOT2_TAG_TYPE_CMDLINE    1
#define MULTIBOOT2_TAG_TYPE_BOOTLOADER_NAME 2
#define MULTIBOOT2_TAG_TYPE_MMAP       6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER 8
#define MULTIBOOT2_MEMORY_AVAILABLE    1

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

#endif