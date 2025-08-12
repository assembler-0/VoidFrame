#ifndef VOIDFRAME_ELFLOADER_H
#define VOIDFRAME_ELFLOADER_H

#pragma once
#include "stdint.h"

typedef struct {
    uint8_t  e_ident[16];    // Magic number and class info
    uint16_t e_type;         // Object file type
    uint16_t e_machine;      // Architecture
    uint32_t e_version;      // Object file version
    uint64_t e_entry;        // Entry point virtual address
    uint64_t e_phoff;        // Program header table offset
    uint64_t e_shoff;        // Section header table offset
    uint32_t e_flags;        // Processor-specific flags
    uint16_t e_ehsize;       // ELF header size
    uint16_t e_phentsize;    // Program header table entry size
    uint16_t e_phnum;        // Program header table entry count
} ElfHeader;

// Program Header
typedef struct {
    uint32_t p_type;         // Segment type (PT_LOAD, etc.)
    uint32_t p_flags;        // Segment flags
    uint64_t p_offset;       // Segment file offset
    uint64_t p_vaddr;        // Segment virtual address
    uint64_t p_paddr;        // Segment physical address
    uint64_t p_filesz;       // Segment size in file
    uint64_t p_memsz;        // Segment size in memory
    uint64_t p_align;        // Segment alignment
} ProgramHeader;

#define ELF_MAGIC 0x464C457F    // "\x7FELF"
#define PT_LOAD   1
#define EM_X86_64 62

// Main functions
int LoadElfFromFile(const char* filename);
void* CreateProcessFromElf(const char* filename);

#endif // VOIDFRAME_ELFLOADER_H
