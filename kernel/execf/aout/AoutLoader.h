#ifndef VOIDFRAME_AOUTLOADER_H
#define VOIDFRAME_AOUTLOADER_H

#pragma once
#include "stdint.h"

// a.out header (32 bytes total - beautifully simple!)
typedef struct {
    uint32_t a_magic;    // Magic number
    uint32_t a_text;     // Size of text segment
    uint32_t a_data;     // Size of data segment  
    uint32_t a_bss;      // Size of BSS segment
    uint32_t a_syms;     // Size of symbol table
    uint32_t a_entry;    // Entry point
    uint32_t a_trsize;   // Text relocation size
    uint32_t a_drsize;   // Data relocation size
} AoutHeader;

// Magic numbers
#define OMAGIC  0407    // Old impure format
#define NMAGIC  0410    // Pure format
#define ZMAGIC  0413    // Demand paged format
#define QMAGIC  0314    // Compact format

// Memory layout constants
#define AOUT_TEXT_START  0x1000     // Traditional text start
#define AOUT_PAGE_SIZE   0x1000     // 4KB pages

// Load options
typedef struct {
    uint8_t privilege_level;
    uint32_t security_flags;
    uint64_t max_memory;
    const char* process_name;
} AoutLoadOptions;

// Functions
int ValidateAoutFile(const uint8_t* aout_data, uint64_t size);
uint32_t CreateProcessFromAout(const char* filename, const AoutLoadOptions* options);

#endif // VOIDFRAME_AOUTLOADER_H