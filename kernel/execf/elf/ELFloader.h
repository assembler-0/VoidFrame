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
    uint16_t e_shentsize;    // Section header table entry size
    uint16_t e_shnum;        // Section header table entry count
    uint16_t e_shstrndx;     // Section header string table index
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

// ELF File Classes
#define ELFCLASS32  1
#define ELFCLASS64  2

// ELF Data Encoding
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2

// ELF File Types
#define ET_NONE     0    // No file type
#define ET_REL      1    // Relocatable file
#define ET_EXEC     2    // Executable file
#define ET_DYN      3    // Shared object file

// Program Header Types
#define PT_NULL     0    // Unused entry
#define PT_LOAD     1    // Loadable segment
#define PT_DYNAMIC  2    // Dynamic linking info
#define PT_INTERP   3    // Interpreter path
#define PT_NOTE     4    // Auxiliary info

// Program Header Flags
#define PF_X        1    // Execute
#define PF_W        2    // Write
#define PF_R        4    // Read

#define ELF_MAGIC   0x464C457F    // "\x7FELF"
#define EM_X86_64   62

// Enhanced API
typedef struct {
    uint8_t privilege_level;    // PROC_PRIV_NORM or PROC_PRIV_SYSTEM
    uint32_t security_flags;    // Additional security flags
    uint64_t max_memory;        // Memory limit for sched
    const char* process_name;   // Optional sched name for debugging
} ElfLoadOptions;

// Main functions
int LoadElfFromFile(const char* filename);
uint32_t CreateProcessFromElf(const char* filename, const ElfLoadOptions* options);
uint32_t LoadElfExecutable(const char* filename); // Simple wrapper
int ValidateElfFile(const uint8_t* elf_data, uint64_t size);

#endif // VOIDFRAME_ELFLOADER_H