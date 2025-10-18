#ifndef VOIDFRAME_EXECLOADER_H
#define VOIDFRAME_EXECLOADER_H

#pragma once
#include "stdint.h"

// Universal executable format detector and loader
typedef struct {
    uint8_t privilege_level;
    uint32_t security_flags;
    uint64_t max_memory;
    const char* process_name;
} ExecLoadOptions;

// Format detection
typedef enum {
    EXEC_FORMAT_UNKNOWN = 0,
    EXEC_FORMAT_ELF64,
    EXEC_FORMAT_PE32PLUS,
    EXEC_FORMAT_MACHO64,
    EXEC_FORMAT_AOUT
} ExecFormat;

// Main API
ExecFormat DetectExecutableFormat(const uint8_t* data, uint64_t size);
uint32_t LoadExecutable(const char* filename, const ExecLoadOptions* options);

#endif // VOIDFRAME_EXECLOADER_H