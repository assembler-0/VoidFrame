#ifndef VOIDFRAME_PELOADER_H
#define VOIDFRAME_PELOADER_H

#pragma once
#include <stdint.h>

// DOS Header (first 64 bytes of PE file)
typedef struct __attribute__((packed))  {
    uint16_t e_magic;      // "MZ"
    uint16_t e_cblp;       // Bytes on last page
    uint16_t e_cp;         // Pages in file
    uint16_t e_crlc;       // Relocations
    uint16_t e_cparhdr;    // Size of header in paragraphs
    uint16_t e_minalloc;   // Minimum extra paragraphs
    uint16_t e_maxalloc;   // Maximum extra paragraphs
    uint16_t e_ss;         // Initial SS value
    uint16_t e_sp;         // Initial SP value
    uint16_t e_csum;       // Checksum
    uint16_t e_ip;         // Initial IP value
    uint16_t e_cs;         // Initial CS value
    uint16_t e_lfarlc;     // Address of relocation table
    uint16_t e_ovno;       // Overlay number
    uint16_t e_res[4];     // Reserved
    uint16_t e_oemid;      // OEM identifier
    uint16_t e_oeminfo;    // OEM information
    uint16_t e_res2[10];   // Reserved
    uint32_t e_lfanew;     // Offset to PE header
} DOSHeader;

// PE Header
typedef struct __attribute__((packed))  {
    uint32_t signature;    // "PE\0\0"
    uint16_t machine;      // Target machine
    uint16_t sections;     // Number of sections
    uint32_t timestamp;    // Time/date stamp
    uint32_t ptr_to_syms;  // Pointer to symbol table
    uint32_t num_syms;     // Number of symbols
    uint16_t opt_hdr_size; // Size of optional header
    uint16_t characteristics; // File characteristics
} PEHeader;

// Optional Header (PE32+)
typedef struct __attribute__((packed))  {
    uint16_t magic;           // PE32+ magic (0x20b)
    uint8_t  major_linker;    // Linker version
    uint8_t  minor_linker;
    uint32_t code_size;       // Size of code
    uint32_t data_size;       // Size of initialized data
    uint32_t bss_size;        // Size of uninitialized data
    uint32_t entry_point;     // Entry point RVA
    uint32_t code_base;       // Base of code
    uint64_t image_base;      // Image base address
    uint32_t section_align;   // Section alignment
    uint32_t file_align;      // File alignment
    uint16_t major_os;        // OS version
    uint16_t minor_os;
    uint16_t major_image;     // Image version
    uint16_t minor_image;
    uint16_t major_subsys;    // Subsystem version
    uint16_t minor_subsys;
    uint32_t win32_version;   // Win32 version
    uint32_t image_size;      // Size of image
    uint32_t headers_size;    // Size of headers
    uint32_t checksum;        // Checksum
    uint16_t subsystem;       // Subsystem
    uint16_t dll_chars;       // DLL characteristics
    uint64_t stack_reserve;   // Stack reserve size
    uint64_t stack_commit;    // Stack commit size
    uint64_t heap_reserve;    // Heap reserve size
    uint64_t heap_commit;     // Heap commit size
    uint32_t loader_flags;    // Loader flags
    uint32_t num_rva_sizes;   // Number of data directories
} OptionalHeader;

// Section Header
typedef struct __attribute__((packed))  {
    char     name[8];         // Section name
    uint32_t virtual_size;    // Virtual size
    uint32_t virtual_addr;    // Virtual address (RVA)
    uint32_t raw_data_size;   // Size of raw data
    uint32_t raw_data_ptr;    // Pointer to raw data
    uint32_t reloc_ptr;       // Pointer to relocations
    uint32_t line_nums_ptr;   // Pointer to line numbers
    uint16_t num_relocs;      // Number of relocations
    uint16_t num_line_nums;   // Number of line numbers
    uint32_t characteristics; // Section characteristics
} SectionHeader;

_Static_assert(sizeof(DOSHeader) == 64, "DOSHeader size mismatch");

// Constants
#define DOS_MAGIC       0x5A4D  // "MZ"
#define PE_MAGIC        0x00004550  // "PE\0\0"
#define PE32PLUS_MAGIC  0x20b   // PE32+ magic

#define IMAGE_FILE_MACHINE_AMD64    0x8664
#define IMAGE_FILE_EXECUTABLE_IMAGE 0x0002

#define IMAGE_SCN_CNT_CODE          0x00000020
#define IMAGE_SCN_CNT_INIT_DATA     0x00000040
#define IMAGE_SCN_CNT_UNINIT_DATA   0x00000080
#define IMAGE_SCN_MEM_EXECUTE       0x20000000
#define IMAGE_SCN_MEM_READ          0x40000000
#define IMAGE_SCN_MEM_WRITE         0x80000000

// Load options (similar to ELF)
typedef struct __attribute__((packed))  {
    uint8_t privilege_level;
    uint32_t security_flags;
    uint64_t max_memory;
    const char* process_name;
} PELoadOptions;

// Main functions
int ValidatePEFile(const uint8_t* pe_data, uint64_t size);
uint32_t CreateProcessFromPE(const char* filename, const PELoadOptions* options);

#endif // VOIDFRAME_PELOADER_H