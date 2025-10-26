#include "CRC32.h"

static uint32_t crc32_table[256];

void CRC32Init() {
    uint32_t polynomial = 0xEDB88320;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (size_t j = 0; j < 8; j++) {
            if (c & 1) {
                c = polynomial ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }
}

uint32_t CRC32(const void* data, size_t length) {
    static int table_generated = 0;
    if (!table_generated) {
        CRC32Init();
        table_generated = 1;
    }

    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;

    for (size_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}
