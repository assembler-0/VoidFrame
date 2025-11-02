#ifndef VOIDFRAME_MACHOLOADER_H
#define VOIDFRAME_MACHOLOADER_H

#include <stdint.h>

typedef struct {
    uint8_t privilege_level;
    uint32_t security_flags;
    uint64_t max_memory;
    const char* process_name;
} MachoLoadOptions;

uint32_t CreateProcessFromMacho(const char* filename, const MachoLoadOptions* options);

#endif //VOIDFRAME_MACHOLOADER_H
