#include "MemOps.h"
#include "Cpu.h"
#include "Io.h"
#include "Panic.h"

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

        __asm__ volatile(
            "vmovq %0, %%xmm0\n"
            "vpbroadcastq %%xmm0, %%ymm0\n"
            :
            : "r"(val64)
            : "xmm0", "ymm0"
        );

        while (size >= 32) {
            __asm__ volatile("vmovdqu %%ymm0, (%0)" : : "r"(d) : "memory");
            d += 32;
            size -= 32;
        }

        // Clean up YMM registers
        __asm__ volatile("vzeroupper" ::: "memory");
    }
    else if (features->sse2 && size >= 16) {
        // Original SSE2 path with better value construction
        uint64_t val64 = 0x0101010101010101ULL * val;

        __asm__ volatile(
            "movq %0, %%xmm0\n"
            "punpcklqdq %%xmm0, %%xmm0\n"
            :
            : "r"(val64)
            : "xmm0"
        );

        while (size >= 16) {
            __asm__ volatile("movdqu %%xmm0, (%0)" : : "r"(d) : "memory");
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
            __asm__ volatile(
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
        __asm__ volatile("vzeroupper" ::: "memory");
    }

    else if (features->sse2 && size >= 16) {
        // SSE2 copy using unaligned load/store. Disable IRQs to avoid ISR clobber.
        irq_flags_t irqf = save_irq_flags();
        cli();
        while (size >= 16) {
            __asm__ volatile(
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
        __asm__ volatile("sfence" ::: "memory");
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
        __asm__ volatile("vpxor %%ymm0, %%ymm0, %%ymm0" ::: "ymm0");

        uint8_t* p = (uint8_t*)page;
        for (int i = 0; i < 4096; i += 32) {
            __asm__ volatile("vmovdqa %%ymm0, (%0)" : : "r"(p + i) : "memory");
        }

        __asm__ volatile("vzeroupper" ::: "memory");
    } else if (features->sse2) {
        __asm__ volatile("pxor %%xmm0, %%xmm0" ::: "xmm0");

        uint8_t* p = (uint8_t*)page;
        // Unroll for better performance
        for (int i = 0; i < 4096; i += 64) {
            __asm__ volatile(
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