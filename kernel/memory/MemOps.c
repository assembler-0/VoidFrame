#include "MemOps.h"
#include "Cpu.h"
#include "Io.h"
#include "Panic.h"

void strcpy(char* dest, const char* src) {
    // Optimize for 64-bit aligned copies when possible
    if (((uintptr_t)dest & 7) == 0 && ((uintptr_t)src & 7) == 0) {
        uint64_t* d64 = (uint64_t*)dest;
        const uint64_t* s64 = (const uint64_t*)src;

        uint64_t val;
        while ((val = *s64++) != 0) {
            // Check if any byte in the 64-bit value is zero
            if ((val & 0xFF00000000000000ULL) == 0 ||
                (val & 0x00FF000000000000ULL) == 0 ||
                (val & 0x0000FF0000000000ULL) == 0 ||
                (val & 0x000000FF00000000ULL) == 0 ||
                (val & 0x00000000FF000000ULL) == 0 ||
                (val & 0x0000000000FF0000ULL) == 0 ||
                (val & 0x000000000000FF00ULL) == 0 ||
                (val & 0x00000000000000FFULL) == 0) {
                // Found null terminator, fall back to byte copy
                char* d = (char*)d64;
                const char* s = (const char*)(s64 - 1);
                while ((*d++ = *s++));
                return;
            }
            *d64++ = val;
        }
        *(char*)d64 = '\0';
    } else {
        // Original byte-by-byte copy for unaligned data
        while ((*dest++ = *src++));
    }
}

void strcat(char* dest, const char* src) {
    // Find end of dest string more efficiently
    while (*dest) dest++;
    strcpy(dest, src);  // Reuse optimized strcpy
}

void htoa(uint64_t n, char* buffer) {
    static const char hex_chars[16] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';

    // Unroll the loop for better performance
    buffer[2]  = hex_chars[(n >> 60) & 0xF];
    buffer[3]  = hex_chars[(n >> 56) & 0xF];
    buffer[4]  = hex_chars[(n >> 52) & 0xF];
    buffer[5]  = hex_chars[(n >> 48) & 0xF];
    buffer[6]  = hex_chars[(n >> 44) & 0xF];
    buffer[7]  = hex_chars[(n >> 40) & 0xF];
    buffer[8]  = hex_chars[(n >> 36) & 0xF];
    buffer[9]  = hex_chars[(n >> 32) & 0xF];
    buffer[10] = hex_chars[(n >> 28) & 0xF];
    buffer[11] = hex_chars[(n >> 24) & 0xF];
    buffer[12] = hex_chars[(n >> 20) & 0xF];
    buffer[13] = hex_chars[(n >> 16) & 0xF];
    buffer[14] = hex_chars[(n >> 12) & 0xF];
    buffer[15] = hex_chars[(n >> 8)  & 0xF];
    buffer[16] = hex_chars[(n >> 4)  & 0xF];
    buffer[17] = hex_chars[n & 0xF];
    buffer[18] = '\0';
}

void itoa(uint64_t n, char* buffer) {
    if (n == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }

    char temp_buffer[21];
    char* p = &temp_buffer[20];
    *p = '\0';

    // Use faster division by avoiding modulo when possible
    while (n >= 10) {
        uint64_t q = n / 10;
        *--p = '0' + (n - q * 10);  // Faster than n % 10
        n = q;
    }
    *--p = '0' + n;

    strcpy(buffer, p);
}

void* memset(void* dest, int value, unsigned long size) {
    return FastMemset(dest, value, size);
}

void* FastMemset(void* dest, int value, uint64_t size) {
    ASSERT(dest != NULL);

    if (size == 0) return dest;

    CpuFeatures* features = GetCpuFeatures();
    uint8_t* d = (uint8_t*)dest;
    uint8_t val = (uint8_t)value;

    // Use AVX2 if available for even better performance
    if (features->avx2 && size >= 32) {
        // Create 256-bit value
        uint64_t val64 = 0x0101010101010101ULL * val;

        asm volatile(
            "vmovq %0, %%xmm0\n"
            "vpbroadcastq %%xmm0, %%ymm0\n"
            :
            : "r"(val64)
            : "xmm0", "ymm0"
        );

        while (size >= 32) {
            asm volatile("vmovdqu %%ymm0, (%0)" : : "r"(d) : "memory");
            d += 32;
            size -= 32;
        }

        // Clean up YMM registers
        asm volatile("vzeroupper" ::: "memory");
    }
    else if (features->sse2 && size >= 16) {
        // Original SSE2 path with better value construction
        uint64_t val64 = 0x0101010101010101ULL * val;

        asm volatile(
            "movq %0, %%xmm0\n"
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
    else if (size >= 8) {
        // 64-bit aligned memset for smaller sizes
        uint64_t val64 = 0x0101010101010101ULL * val;

        while (size >= 8 && ((uintptr_t)d & 7) == 0) {
            *(uint64_t*)d = val64;
            d += 8;
            size -= 8;
        }
    }

    // Handle remaining bytes
    while (size--) *d++ = val;
    return dest;
}

void* FastMemcpy(void* dest, const void* src, uint64_t size) {
    ASSERT(dest != NULL && src != NULL);

    if (size == 0) return dest;

    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    CpuFeatures* features = GetCpuFeatures();

    // Use AVX2 for large copies if available
    if (features->avx2 && size >= 32) {
        // AVX2 copy using an unaligned load and store for maximum safety.
        // We save and restore ymm7 to be invisible to the calling code.
        while (size >= 32) {
            asm volatile(
                "vmovdqu (%1), %%ymm7\n"  // Unaligned read from src
                "vmovdqu %%ymm7, (%0)\n"  // Unaligned write to dest
                :
                : "r"(d), "r"(s)
                : "memory", "ymm7"
            );
            d += 32;
            s += 32;
            size -= 32;
        }

        // IMPORTANT: Clean up the AVX state to prevent performance issues
        // when mixing with older SSE code.
        asm volatile("vzeroupper" ::: "memory");
    }

    else if (features->sse2 && size >= 16) {
        // SSE2 copy using unaligned load/store. Disable IRQs to avoid ISR clobber.
        irq_flags_t irqf = save_irq_flags();
        cli();
        while (size >= 16) {
            asm volatile(
                "movdqu (%1), %%xmm7\n"   // Unaligned read from src
                "movdqu %%xmm7, (%0)\n"   // Unaligned write to dest
                :
                : "r"(d), "r"(s)
                : "memory", "xmm7"
            );
            d += 16;
            s += 16;
            size -= 16;
        }
        asm volatile("sfence" ::: "memory");
        restore_irq_flags(irqf);
    }

    if (size >= 8) {
        // Byte-align destination first
        while (((uintptr_t)d & 7) != 0 && size > 0) {
            *d++ = *s++;
            size--;
        }

        // If src is also aligned, we can use fast 64-bit moves
        if (((uintptr_t)s & 7) == 0) {
            uint64_t* d64 = (uint64_t*)d;
            const uint64_t* s64 = (const uint64_t*)s;

            while (size >= 64) {
                d64[0] = s64[0]; d64[1] = s64[1]; d64[2] = s64[2]; d64[3] = s64[3];
                d64[4] = s64[4]; d64[5] = s64[5]; d64[6] = s64[6]; d64[7] = s64[7];
                d64 += 8;
                s64 += 8;
                size -= 64;
            }
            while (size >= 8) {
                *d64++ = *s64++;
                size -= 8;
            }
            d = (uint8_t*)d64;
            s = (const uint8_t*)s64;
        }
    }

    // Handle the remainder
    while (size > 0) {
        *d++ = *s++;
        size--;
    }

    return dest;
}

void FastZeroPage(void* page) {
    ASSERT(page != NULL);
    CpuFeatures* features = GetCpuFeatures();

    if (features->avx2) {
        // Use AVX2 for faster page zeroing
        asm volatile("vpxor %%ymm0, %%ymm0, %%ymm0" ::: "ymm0");

        uint8_t* p = (uint8_t*)page;
        for (int i = 0; i < 4096; i += 32) {
            asm volatile("vmovdqa %%ymm0, (%0)" : : "r"(p + i) : "memory");
        }

        asm volatile("vzeroupper" ::: "memory");
    } else if (features->sse2) {
        asm volatile("pxor %%xmm0, %%xmm0" ::: "xmm0");

        uint8_t* p = (uint8_t*)page;
        // Unroll for better performance
        for (int i = 0; i < 4096; i += 64) {
            asm volatile(
                "movdqa %%xmm0, 0(%0)\n"
                "movdqa %%xmm0, 16(%0)\n"
                "movdqa %%xmm0, 32(%0)\n"
                "movdqa %%xmm0, 48(%0)\n"
                : : "r"(p + i) : "memory"
            );
        }
    } else {
        // Fallback to optimized memset
        FastMemset(page, 0, 4096);
    }
}

int FastMemcmp(const void* ptr1, const void* ptr2, uint64_t size) {
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;

    // 64-bit comparison for aligned data
    if (size >= 8 && ((uintptr_t)p1 & 7) == 0 && ((uintptr_t)p2 & 7) == 0) {
        const uint64_t* q1 = (const uint64_t*)p1;
        const uint64_t* q2 = (const uint64_t*)p2;

        while (size >= 8) {
            if (*q1 != *q2) {
                // Found difference, need to find which byte
                p1 = (const uint8_t*)q1;
                p2 = (const uint8_t*)q2;
                for (int i = 0; i < 8; i++) {
                    if (p1[i] < p2[i]) return -1;
                    if (p1[i] > p2[i]) return 1;
                }
            }
            q1++;
            q2++;
            size -= 8;
        }
        p1 = (const uint8_t*)q1;
        p2 = (const uint8_t*)q2;
    }

    // Compare remaining bytes
    while (size > 0) {
        if (*p1 < *p2) return -1;
        if (*p1 > *p2) return 1;
        p1++;
        p2++;
        size--;
    }
    return 0;
}