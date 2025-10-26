#ifndef VOIDFRAME_CRC32_H
#define VOIDFRAME_CRC32_H

#include <stdint.h>
#include <stddef.h>

uint32_t CRC32(const void* data, size_t length);
void CRC32Init();

#endif //VOIDFRAME_CRC32_H
