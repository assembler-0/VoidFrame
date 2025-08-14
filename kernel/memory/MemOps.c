#include "MemOps.h"
#include "Cpu.h"
#include "Panic.h"

void strcpy(char* dest, const char* src) {
    while ((*dest++ = *src++));
}

void strcat(char* dest, const char* src) {
    while (*dest) dest++;
    while ((*dest++ = *src++));
}

void htoa(uint64_t n, char* buffer) {
    const char* hex_chars = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buffer[2 + i] = hex_chars[(n >> (60 - i * 4)) & 0xF];
    }
    buffer[18] = '\0';
}

void itoa(uint64_t n, char* buffer) {
    if (n == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    char int_buffer[21];
    char* p = &int_buffer[20];
    *p = '\0';
    uint64_t temp = n;
    do {
        *--p = '0' + (temp % 10);
        temp /= 10;
    } while(temp > 0);
    strcpy(buffer, p);
}

void* memset(void* dest, int value, unsigned long size) { // FCK GCC
    return FastMemset(dest, value, size);
}

void* FastMemset(void* dest, int value, uint64_t size) {
    ASSERT(dest != NULL);
    CpuFeatures* features = GetCpuFeatures();
    uint8_t* d = (uint8_t*)dest;

    if (features->sse2 && size >= 16) {
        // Create a 128-bit value where all bytes are 'value'
        uint64_t val64 = ((uint64_t)value << 56) | ((uint64_t)value << 48) |
                         ((uint64_t)value << 40) | ((uint64_t)value << 32) |
                         ((uint64_t)value << 24) | ((uint64_t)value << 16) |
                         ((uint64_t)value << 8) | value;

        asm volatile(
            "movq %0, %%xmm0\n"
            "punpcklqdq %%xmm0, %%xmm0\n"
            "punpcklqdq %%xmm0, %%xmm0\n"
            :
            : "r"(val64)
            : "xmm0"
        );

        while (size >= 16) {
            asm volatile("movdqu %%xmm0, (%0)" : : "r"(d) : "memory");
            d += 16;
            size -= 16;
        }
    }

    // Handle remaining bytes
    while (size--) *d++ = value;
    return dest;
}

void* FastMemcpy(void* dest, const void* src, uint64_t size) {
    ASSERT(dest != NULL && src != NULL);
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (size >= 8) {
        // Handle alignment
        while (((uintptr_t)d & 7) != 0 && size > 0) {
            *d++ = *s++;
            size--;
        }

        if (((uintptr_t)s & 7) == 0) {
            // Both aligned - use 64-bit copies with loop unrolling
            uint64_t* d64 = (uint64_t*)d;
            const uint64_t* s64 = (const uint64_t*)s;

            // Unrolled loop for better performance
            while (size >= 32) {
                d64[0] = s64[0];  // Copy 32 bytes
                d64[1] = s64[1];  // in 4 operations
                d64[2] = s64[2];
                d64[3] = s64[3];
                d64 += 4;
                s64 += 4;
                size -= 32;
            }

            while (size >= 8) {
                *d64++ = *s64++;
                size -= 8;
            }

            d = (uint8_t*)d64;
            s = (const uint8_t*)s64;
        } else {
            // Source not aligned - use 32-bit copies
            while (size >= 4 && ((uintptr_t)s & 3) == 0) {
                *(uint32_t*)d = *(const uint32_t*)s;
                d += 4;
                s += 4;
                size -= 4;
            }
        }
    }

    // Handle remainder bytes
    while (size > 0) {
        *d++ = *s++;
        size--;
    }

    return dest;
}

void FastZeroPage(void* page) {
    ASSERT(page != NULL);
    CpuFeatures* features = GetCpuFeatures();

    if (features->sse2) {
        asm volatile("pxor %%xmm0, %%xmm0" ::: "xmm0");

        uint8_t* p = (uint8_t*)page;
        for (int i = 0; i < 4096; i += 16) {
            asm volatile("movdqu %%xmm0, (%0)" : : "r"(p + i) : "memory");
        }
    } else {
        FastMemset(page, 0, 4096);
    }
}

int FastMemcmp(const void* ptr1, const void* ptr2, uint64_t size) {
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;

    for (uint64_t i = 0; i < size; i++) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}