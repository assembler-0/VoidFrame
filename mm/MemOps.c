#include "MemOps.h"
#include "Io.h"
#include "Panic.h"
#include "x64.h"

#define PREFETCH_DISTANCE 512
#define PREFETCH_LINES 8

// Non-temporal store threshold (use NT stores for large copies to avoid cache pollution)
#define NT_STORE_THRESHOLD (4*1024*1024)  // 4MB - more aggressive for better cache behavior
#define LARGE_COPY_THRESHOLD (64*1024)    // 64KB - switch to optimized large copy
#define ALIGNMENT_THRESHOLD 128            // Minimum size to justify alignment overhead

void* memset(void* restrict dest, const int value, const unsigned long size) {
    return FastMemset(dest, value, size);
}

void* memcpy(void* restrict dest, const void* restrict src, const unsigned long size) {
    return FastMemcpy(dest, src, size);
}

int memcmp(const void* restrict s1, const void* restrict s2, const unsigned long size) {
    return FastMemcmp(s1, s2, size);
}

void* FastMemset(void* restrict dest, int value, uint64_t size) {
    ASSERT(dest != NULL);

    if (size == 0) return dest;

    CpuFeatures* features = GetCpuFeatures();
    uint8_t* d = dest;
    uint8_t val = (uint8_t)value;

    // Handle small sizes with optimized path
    if (size < 32) {
        // Use overlapping stores for sizes 16-31
        if (size >= 16) {
            uint64_t val64 = 0x0101010101010101ULL * val;
            __asm__ volatile(
                "movq %0, %%xmm0\n"
                "punpcklqdq %%xmm0, %%xmm0\n"
                "movdqu %%xmm0, (%1)\n"
                "movdqu %%xmm0, -16(%1,%2)\n"
                :
                : "r"(val64), "r"(d), "r"(size)
                : "xmm0", "memory"
            );
            return dest;
        }

        // For 8-15 bytes, use overlapping 64-bit stores
        if (size >= 8) {
            uint64_t val64 = 0x0101010101010101ULL * val;
            *(uint64_t*)d = val64;
            *(uint64_t*)(d + size - 8) = val64;
            return dest;
        }

        // For 4-7 bytes, use overlapping 32-bit stores
        if (size >= 4) {
            uint32_t val32 = 0x01010101U * val;
            *(uint32_t*)d = val32;
            *(uint32_t*)(d + size - 4) = val32;
            return dest;
        }

        // For 1-3 bytes
        while (size--) *d++ = val;
        return dest;
    }

    // AVX-512 path (if available) - highest performance
    if (features->avx512f && size >= 64) {
        uint64_t val64 = 0x0101010101010101ULL * val;

        __asm__ volatile(
            "vmovq %0, %%xmm0\n"
            "vpbroadcastq %%xmm0, %%zmm0\n"
            :
            : "r"(val64)
            : "xmm0", "zmm0"
        );

        // Align to 64 bytes if beneficial
        if (size >= ALIGNMENT_THRESHOLD) {
            uintptr_t misalign = (uintptr_t)d & 63;
            if (misalign) {
                misalign = 64 - misalign;
                size -= misalign;
                // Use SIMD for alignment when possible
                if (misalign >= 16) {
                    __asm__ volatile(
                        "vmovq %1, %%xmm1\n"
                        "vpbroadcastq %%xmm1, %%xmm1\n"
                        "movdqu %%xmm1, (%0)\n"
                        : "+r"(d)
                        : "r"(val64)
                        : "xmm1", "memory"
                    );
                    d += 16;
                    misalign -= 16;
                }
                while (misalign--) *d++ = val;
            }
        }

        // Use non-temporal stores for very large buffers
        if (size >= NT_STORE_THRESHOLD) {
            _full_mem_prot_start();
            
            // Ensure cache line alignment for NT operations
            while ((uintptr_t)d & 63) {
                *d++ = val;
                size--;
            }
            
            // 8x unroll with prefetch for maximum throughput
            while (size >= 512) {
                __asm__ volatile(
                    "prefetchnta 512(%0)\n"
                    "vmovntdq %%zmm0, (%0)\n"
                    "vmovntdq %%zmm0, 64(%0)\n"
                    "vmovntdq %%zmm0, 128(%0)\n"
                    "vmovntdq %%zmm0, 192(%0)\n"
                    "vmovntdq %%zmm0, 256(%0)\n"
                    "vmovntdq %%zmm0, 320(%0)\n"
                    "vmovntdq %%zmm0, 384(%0)\n"
                    "vmovntdq %%zmm0, 448(%0)\n"
                    :
                    : "r"(d)
                    : "memory"
                );
                d += 512;
                size -= 512;
            }
            
            while (size >= 64) {
                __asm__ volatile("vmovntdq %%zmm0, (%0)" : : "r"(d) : "memory");
                d += 64;
                size -= 64;
            }
#ifdef VF_CONFIG_INTEL
            _full_mem_prot_end_intel();
#else
            _full_mem_prot_end();
#endif
        } else {
            while (size >= 64) {
                __asm__ volatile("vmovdqu64 %%zmm0, (%0)" : : "r"(d) : "memory");
                d += 64;
                size -= 64;
            }
        }

        __asm__ volatile("vzeroupper" ::: "memory");
    }
    // AVX2 path with optimizations
    else if (features->avx2 && size >= 32) {
        uint64_t val64 = 0x0101010101010101ULL * val;

        __asm__ volatile(
            "vmovq %0, %%xmm0\n"
            "vpbroadcastq %%xmm0, %%ymm0\n"
            :
            : "r"(val64)
            : "xmm0", "ymm0"
        );

        // Align to 32 bytes for better performance on aligned stores
        if (size >= 64) {
            while ((uintptr_t)d & 31) {
                *d++ = val;
                size--;
            }
        }

        // 8x unroll for better throughput with prefetch
        while (size >= 256) {
            __asm__ volatile(
                "prefetchnta 256(%0)\n"
                "vmovdqa %%ymm0, (%0)\n"
                "vmovdqa %%ymm0, 32(%0)\n"
                "vmovdqa %%ymm0, 64(%0)\n"
                "vmovdqa %%ymm0, 96(%0)\n"
                "vmovdqa %%ymm0, 128(%0)\n"
                "vmovdqa %%ymm0, 160(%0)\n"
                "vmovdqa %%ymm0, 192(%0)\n"
                "vmovdqa %%ymm0, 224(%0)\n"
                :
                : "r"(d)
                : "memory"
            );
            d += 256;
            size -= 256;
        }
        
        while (size >= 128) {
            __asm__ volatile(
                "vmovdqa %%ymm0, (%0)\n"
                "vmovdqa %%ymm0, 32(%0)\n"
                "vmovdqa %%ymm0, 64(%0)\n"
                "vmovdqa %%ymm0, 96(%0)\n"
                :
                : "r"(d)
                : "memory"
            );
            d += 128;
            size -= 128;
        }

        while (size >= 32) {
            __asm__ volatile("vmovdqu %%ymm0, (%0)" : : "r"(d) : "memory");
            d += 32;
            size -= 32;
        }

        __asm__ volatile("vzeroupper" ::: "memory");
    }
    // SSE2 path
    else if (features->sse2 && size >= 16) {
        uint64_t val64 = 0x0101010101010101ULL * val;

        __asm__ volatile(
            "movq %0, %%xmm0\n"
            "punpcklqdq %%xmm0, %%xmm0\n"
            :
            : "r"(val64)
            : "xmm0"
        );

        // Unroll 4x
        while (size >= 64) {
            __asm__ volatile(
                "movdqu %%xmm0, (%0)\n"
                "movdqu %%xmm0, 16(%0)\n"
                "movdqu %%xmm0, 32(%0)\n"
                "movdqu %%xmm0, 48(%0)\n"
                :
                : "r"(d)
                : "memory"
            );
            d += 64;
            size -= 64;
        }

        while (size >= 16) {
            __asm__ volatile("movdqu %%xmm0, (%0)" : : "r"(d) : "memory");
            d += 16;
            size -= 16;
        }
    }

    // Handle remaining bytes with 64-bit stores when possible
    if (size >= 8) {
        uint64_t val64 = 0x0101010101010101ULL * val;
        while (size >= 8) {
            *(uint64_t*)d = val64;
            d += 8;
            size -= 8;
        }
    }

    // Final bytes
    while (size--) *d++ = val;
    return dest;
}

void* FastMemcpy(void* restrict dest, const void* restrict src, uint64_t size) {
    ASSERT(dest != NULL && src != NULL);

    if (size == 0) return dest;

    uint8_t* d = dest;
    const uint8_t* s = src;

    // Handle overlap cases
    if (d == s || !d || !s) return dest;

    // If regions overlap and destination starts within source, perform backward copy to avoid corruption
    if (d > s && d < s + size) {
        d += size;
        s += size;
        while (size >= 8) {
            d -= 8; s -= 8;
            *(uint64_t*)d = *(const uint64_t*)s;
            size -= 8;
        }
        while (size--) {
            d--; s--;
            *d = *s;
        }
        return dest;
    }

    CpuFeatures* features = GetCpuFeatures();

    // Small copy optimization using overlapping loads/stores
    if (size < 32) {
        if (size >= 16) {
            __asm__ volatile(
                "movdqu (%1), %%xmm0\n"
                "movdqu -16(%1,%2), %%xmm1\n"
                "movdqu %%xmm0, (%0)\n"
                "movdqu %%xmm1, -16(%0,%2)\n"
                :
                : "r"(d), "r"(s), "r"(size)
                : "xmm0", "xmm1", "memory"
            );
            return dest;
        }

        if (size >= 8) {
            *(uint64_t*)d = *(uint64_t*)s;
            *(uint64_t*)(d + size - 8) = *(uint64_t*)(s + size - 8);
            return dest;
        }

        if (size >= 4) {
            *(uint32_t*)d = *(uint32_t*)s;
            *(uint32_t*)(d + size - 4) = *(uint32_t*)(s + size - 4);
            return dest;
        }

        while (size-- && d && s) *d++ = *s++;
        return dest;
    }

    // AVX-512 path
    if (features->avx512f && size >= 64) {
        // Align destination to 64 bytes if beneficial
        if (size >= ALIGNMENT_THRESHOLD) {
            uintptr_t misalign = (uintptr_t)d & 63;
            if (misalign) {
                misalign = 64 - misalign;
                size -= misalign;
                // Use SIMD for alignment when possible
                while (misalign >= 16 && d && s) {
                    __asm__ volatile(
                        "movdqu (%1), %%xmm0\n"
                        "movdqu %%xmm0, (%0)\n"
                        : "+r"(d), "+r"(s)
                        :
                        : "xmm0", "memory"
                    );
                    d += 16; s += 16;
                    misalign -= 16;
                }
                while (misalign-- && d && s) *d++ = *s++;
            }
        }

        // Use non-temporal stores for very large copies
        if (size >= NT_STORE_THRESHOLD) {
            _full_mem_prot_start();
            
            // Ensure cache line alignment for NT operations
            while ((uintptr_t)d & 63 && d && s) {
                *d++ = *s++;
                size--;
            }
            
            // 8x unroll with aggressive prefetching
            while (size >= 512 && d && s) {
                __asm__ volatile(
                    "prefetchnta 512(%1)\n"
                    "prefetchnta 576(%1)\n"
                    "prefetchnta 640(%1)\n"
                    "prefetchnta 704(%1)\n"
                    "vmovdqu64 (%1), %%zmm0\n"
                    "vmovdqu64 64(%1), %%zmm1\n"
                    "vmovdqu64 128(%1), %%zmm2\n"
                    "vmovdqu64 192(%1), %%zmm3\n"
                    "vmovdqu64 256(%1), %%zmm4\n"
                    "vmovdqu64 320(%1), %%zmm5\n"
                    "vmovdqu64 384(%1), %%zmm6\n"
                    "vmovdqu64 448(%1), %%zmm7\n"
                    "vmovntdq %%zmm0, (%0)\n"
                    "vmovntdq %%zmm1, 64(%0)\n"
                    "vmovntdq %%zmm2, 128(%0)\n"
                    "vmovntdq %%zmm3, 192(%0)\n"
                    "vmovntdq %%zmm4, 256(%0)\n"
                    "vmovntdq %%zmm5, 320(%0)\n"
                    "vmovntdq %%zmm6, 384(%0)\n"
                    "vmovntdq %%zmm7, 448(%0)\n"
                    :
                    : "r"(d), "r"(s)
                    : "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "memory"
                );
                d += 512;
                s += 512;
                size -= 512;
            }
            
            while (size >= 256 && d && s) {
                __asm__ volatile(
                    "prefetchnta 256(%1)\n"
                    "prefetchnta 320(%1)\n"
                    "vmovdqu64 (%1), %%zmm0\n"
                    "vmovdqu64 64(%1), %%zmm1\n"
                    "vmovdqu64 128(%1), %%zmm2\n"
                    "vmovdqu64 192(%1), %%zmm3\n"
                    "vmovntdq %%zmm0, (%0)\n"
                    "vmovntdq %%zmm1, 64(%0)\n"
                    "vmovntdq %%zmm2, 128(%0)\n"
                    "vmovntdq %%zmm3, 192(%0)\n"
                    :
                    : "r"(d), "r"(s)
                    : "zmm0", "zmm1", "zmm2", "zmm3", "memory"
                );
                d += 256;
                s += 256;
                size -= 256;
            }
#ifdef VF_CONFIG_INTEL
            _full_mem_prot_end_intel();
#else
            _full_mem_prot_end();
#endif
        } else {
            // Regular copy with 8x unrolling and prefetch
            while (size >= 512 && d && s) {
                __asm__ volatile(
                    "prefetchnta 512(%1)\n"
                    "vmovdqu64 (%1), %%zmm0\n"
                    "vmovdqu64 64(%1), %%zmm1\n"
                    "vmovdqu64 128(%1), %%zmm2\n"
                    "vmovdqu64 192(%1), %%zmm3\n"
                    "vmovdqu64 256(%1), %%zmm4\n"
                    "vmovdqu64 320(%1), %%zmm5\n"
                    "vmovdqu64 384(%1), %%zmm6\n"
                    "vmovdqu64 448(%1), %%zmm7\n"
                    "vmovdqu64 %%zmm0, (%0)\n"
                    "vmovdqu64 %%zmm1, 64(%0)\n"
                    "vmovdqu64 %%zmm2, 128(%0)\n"
                    "vmovdqu64 %%zmm3, 192(%0)\n"
                    "vmovdqu64 %%zmm4, 256(%0)\n"
                    "vmovdqu64 %%zmm5, 320(%0)\n"
                    "vmovdqu64 %%zmm6, 384(%0)\n"
                    "vmovdqu64 %%zmm7, 448(%0)\n"
                    :
                    : "r"(d), "r"(s)
                    : "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7", "memory"
                );
                d += 512;
                s += 512;
                size -= 512;
            }
            
            while (size >= 256 && d && s) {
                __asm__ volatile(
                    "vmovdqu64 (%1), %%zmm0\n"
                    "vmovdqu64 64(%1), %%zmm1\n"
                    "vmovdqu64 128(%1), %%zmm2\n"
                    "vmovdqu64 192(%1), %%zmm3\n"
                    "vmovdqu64 %%zmm0, (%0)\n"
                    "vmovdqu64 %%zmm1, 64(%0)\n"
                    "vmovdqu64 %%zmm2, 128(%0)\n"
                    "vmovdqu64 %%zmm3, 192(%0)\n"
                    :
                    : "r"(d), "r"(s)
                    : "zmm0", "zmm1", "zmm2", "zmm3", "memory"
                );
                d += 256;
                s += 256;
                size -= 256;
            }
        }

        while (size >= 64 && d && s) {
            __asm__ volatile(
                "vmovdqu64 (%1), %%zmm0\n"
                "vmovdqu64 %%zmm0, (%0)\n"
                :
                : "r"(d), "r"(s)
                : "zmm0", "memory"
            );
            d += 64;
            s += 64;
            size -= 64;
        }

        __asm__ volatile("vzeroupper" ::: "memory");
    }
    // AVX2 path with enhanced performance
    else if (features->avx2 && size >= 32) {
        // Align destination for better performance
        if (size >= ALIGNMENT_THRESHOLD) {
            uintptr_t misalign = (uintptr_t)d & 31;
            if (misalign) {
                misalign = 32 - misalign;
                size -= misalign;
                while (misalign >= 16 && d && s) {
                    __asm__ volatile(
                        "movdqu (%1), %%xmm0\n"
                        "movdqu %%xmm0, (%0)\n"
                        : "+r"(d), "+r"(s)
                        :
                        : "xmm0", "memory"
                    );
                    d += 16; s += 16;
                    misalign -= 16;
                }
                while (misalign-- && d && s) *d++ = *s++;
            }
        }

        // 8x unrolled loop with prefetching
        while (size >= 256 && d && s) {
            __asm__ volatile(
                "prefetchnta 256(%1)\n"
                "prefetchnta 320(%1)\n"
                "vmovdqu (%1), %%ymm0\n"
                "vmovdqu 32(%1), %%ymm1\n"
                "vmovdqu 64(%1), %%ymm2\n"
                "vmovdqu 96(%1), %%ymm3\n"
                "vmovdqu 128(%1), %%ymm4\n"
                "vmovdqu 160(%1), %%ymm5\n"
                "vmovdqu 192(%1), %%ymm6\n"
                "vmovdqu 224(%1), %%ymm7\n"
                "vmovdqa %%ymm0, (%0)\n"
                "vmovdqa %%ymm1, 32(%0)\n"
                "vmovdqa %%ymm2, 64(%0)\n"
                "vmovdqa %%ymm3, 96(%0)\n"
                "vmovdqa %%ymm4, 128(%0)\n"
                "vmovdqa %%ymm5, 160(%0)\n"
                "vmovdqa %%ymm6, 192(%0)\n"
                "vmovdqa %%ymm7, 224(%0)\n"
                :
                : "r"(d), "r"(s)
                : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "memory"
            );
            d += 256;
            s += 256;
            size -= 256;
        }
        
        while (size >= 128 && d && s) {
            __asm__ volatile(
                "prefetchnta 256(%1)\n"
                "vmovdqu (%1), %%ymm0\n"
                "vmovdqu 32(%1), %%ymm1\n"
                "vmovdqu 64(%1), %%ymm2\n"
                "vmovdqu 96(%1), %%ymm3\n"
                "vmovdqa %%ymm0, (%0)\n"
                "vmovdqa %%ymm1, 32(%0)\n"
                "vmovdqa %%ymm2, 64(%0)\n"
                "vmovdqa %%ymm3, 96(%0)\n"
                :
                : "r"(d), "r"(s)
                : "ymm0", "ymm1", "ymm2", "ymm3", "memory"
            );
            d += 128;
            s += 128;
            size -= 128;
        }

        while (size >= 32 && d && s) {
            __asm__ volatile(
                "vmovdqu (%1), %%ymm0\n"
                "vmovdqu %%ymm0, (%0)\n"
                :
                : "r"(d), "r"(s)
                : "ymm0", "memory"
            );
            d += 32;
            s += 32;
            size -= 32;
        }

        __asm__ volatile("vzeroupper" ::: "memory");
    }
    // SSE2 path with unrolling
    else if (features->sse2 && size >= 16) {

        // 4x unrolled
        while (size >= 64 && d && s) {
            __asm__ volatile(
                "movdqu (%1), %%xmm0\n"
                "movdqu 16(%1), %%xmm1\n"
                "movdqu 32(%1), %%xmm2\n"
                "movdqu 48(%1), %%xmm3\n"
                "movdqu %%xmm0, (%0)\n"
                "movdqu %%xmm1, 16(%0)\n"
                "movdqu %%xmm2, 32(%0)\n"
                "movdqu %%xmm3, 48(%0)\n"
                :
                : "r"(d), "r"(s)
                : "xmm0", "xmm1", "xmm2", "xmm3", "memory"
            );
            d += 64;
            s += 64;
            size -= 64;
        }

        while (size >= 16 && d && s) {
            __asm__ volatile(
                "movdqu (%1), %%xmm0\n"
                "movdqu %%xmm0, (%0)\n"
                :
                : "r"(d), "r"(s)
                : "xmm0", "memory"
            );
            d += 16;
            s += 16;
            size -= 16;
        }

    }

    // Handle remainder with 64-bit copies when possible
    while (size >= 8 && d && s) {
        *(uint64_t*)d = *(uint64_t*)s;
        d += 8;
        s += 8;
        size -= 8;
    }

    // Final bytes
    while (size-- && d && s) *d++ = *s++;

    return dest;
}

void FastZeroPage(void* restrict page) {
    ASSERT(page != NULL);
    CpuFeatures* features = GetCpuFeatures();

    // AVX-512 path - fastest for page zeroing
    if (features->avx512f) {
        irq_flags_t irqf = save_irq_flags();
        cli();

        __asm__ volatile("vpxorq %%zmm0, %%zmm0, %%zmm0" ::: "zmm0");

        uint8_t* p = page;
        // Unroll 4x for better throughput
        for (int i = 0; i < 4096; i += 256) {
            __asm__ volatile(
                "vmovdqa64 %%zmm0, (%0)\n"
                "vmovdqa64 %%zmm0, 64(%0)\n"
                "vmovdqa64 %%zmm0, 128(%0)\n"
                "vmovdqa64 %%zmm0, 192(%0)\n"
                :
                : "r"(p + i)
                : "memory"
            );
        }

        __asm__ volatile("vzeroupper" ::: "memory");
        __asm__ volatile("mfence" ::: "memory");  // Full memory barrier for page operations  
        restore_irq_flags(irqf);
    }
    // AVX2 path with better unrolling
    else if (features->avx2) {
        irq_flags_t irqf = save_irq_flags();
        cli();

        __asm__ volatile("vpxor %%ymm0, %%ymm0, %%ymm0" ::: "ymm0");

        uint8_t* p = page;
        // Unroll 8x for maximum throughput
        for (int i = 0; i < 4096; i += 256) {
            __asm__ volatile(
                "vmovdqa %%ymm0, (%0)\n"
                "vmovdqa %%ymm0, 32(%0)\n"
                "vmovdqa %%ymm0, 64(%0)\n"
                "vmovdqa %%ymm0, 96(%0)\n"
                "vmovdqa %%ymm0, 128(%0)\n"
                "vmovdqa %%ymm0, 160(%0)\n"
                "vmovdqa %%ymm0, 192(%0)\n"
                "vmovdqa %%ymm0, 224(%0)\n"
                :
                : "r"(p + i)
                : "memory"
            );
        }

        __asm__ volatile("vzeroupper" ::: "memory");
        __asm__ volatile("mfence" ::: "memory");  // Full memory barrier for page operations
        restore_irq_flags(irqf);
    }
    else if (features->sse2) {
        irq_flags_t irqf = save_irq_flags();
        cli();

        __asm__ volatile("pxor %%xmm0, %%xmm0" ::: "xmm0");

        uint8_t* p = page;
        // Unroll 8x
        for (int i = 0; i < 4096; i += 128) {
            __asm__ volatile(
                "movdqa %%xmm0, (%0)\n"
                "movdqa %%xmm0, 16(%0)\n"
                "movdqa %%xmm0, 32(%0)\n"
                "movdqa %%xmm0, 48(%0)\n"
                "movdqa %%xmm0, 64(%0)\n"
                "movdqa %%xmm0, 80(%0)\n"
                "movdqa %%xmm0, 96(%0)\n"
                "movdqa %%xmm0, 112(%0)\n"
                :
                : "r"(p + i)
                : "memory"
            );
        }

        __asm__ volatile("mfence" ::: "memory");  // Full memory barrier for page operations
        restore_irq_flags(irqf);
    }
    else {
        FastMemset(page, 0, 4096);
    }
}

int FastMemcmp(const void* restrict ptr1, const void* restrict ptr2, uint64_t size) {
    if (size == 0) return 0;
    ASSERT(ptr1 != NULL && ptr2 != NULL);

    const uint8_t* p1 = ptr1;
    const uint8_t* p2 = ptr2;
    CpuFeatures* features = GetCpuFeatures();

    // AVX-512 comparison for large blocks
    if (features->avx512f && size >= 64 && p1 && p2) {
        while (size >= 64) {
            uint64_t mask;
            __asm__ volatile(
                "vmovdqu64 (%1), %%zmm0\n"
                "vmovdqu64 (%2), %%zmm1\n"
                "vpcmpub $4, %%zmm0, %%zmm1, %%k1\n"  // Not equal comparison
                "kmovq %%k1, %0\n"
                : "=r"(mask)
                : "r"(p1), "r"(p2)
                : "zmm0", "zmm1", "k1"
            );

            if (mask != 0) {
                // Find first differing byte
                int idx = __builtin_ctzll(mask);
                if (idx < 64) {
                    return p1[idx] < p2[idx] ? -1 : 1;
                }
            }

            p1 += 64;
            p2 += 64;
            size -= 64;
        }
        __asm__ volatile("vzeroupper" ::: "memory");
    }
    // AVX2 comparison
    else if (features->avx2 && size >= 32 && p1 && p2) {
        while (size >= 32) {
            int result;
            __asm__ volatile(
                "vmovdqu (%1), %%ymm0\n"
                "vmovdqu (%2), %%ymm1\n"
                "vpcmpeqb %%ymm0, %%ymm1, %%ymm2\n"
                "vpmovmskb %%ymm2, %0\n"
                : "=r"(result)
                : "r"(p1), "r"(p2)
                : "ymm0", "ymm1", "ymm2"
            );

            if (result != -1) {
                // Find first differing byte
                int idx = __builtin_ctz(~result);
                if (idx < 32) {
                    return p1[idx] < p2[idx] ? -1 : 1;
                }
            }

            p1 += 32;
            p2 += 32;
            size -= 32;
        }
        __asm__ volatile("vzeroupper" ::: "memory");
    }
    // SSE2 comparison
    else if (features->sse2 && size >= 16 && p1 && p2) {
        while (size >= 16) {
            int result;
            __asm__ volatile(
                "movdqu (%1), %%xmm0\n"
                "movdqu (%2), %%xmm1\n"
                "pcmpeqb %%xmm0, %%xmm1\n"
                "pmovmskb %%xmm1, %0\n"
                : "=r"(result)
                : "r"(p1), "r"(p2)
                : "xmm0", "xmm1"
            );

            if (result != 0xFFFF) {
                // Find first differing byte
                int idx = __builtin_ctz(~result);
                if (idx < 16) {
                    return p1[idx] < p2[idx] ? -1 : 1;
                }
            }

            p1 += 16;
            p2 += 16;
            size -= 16;
        }
    }

    // 64-bit comparison for aligned data
    if (size >= 8 && ((uintptr_t)p1 & 7) == 0 && ((uintptr_t)p2 & 7) == 0) {
        const uint64_t* q1 = (const uint64_t*)p1;
        const uint64_t* q2 = (const uint64_t*)p2;

        while (size >= 8 && q1 && q2) {
            uint64_t v1 = *q1;
            uint64_t v2 = *q2;

            if (v1 != v2) {
                // Use bswap to compare in big-endian order (byte-by-byte)
                v1 = __builtin_bswap64(v1);
                v2 = __builtin_bswap64(v2);
                return v1 < v2 ? -1 : 1;
            }

            q1++;
            q2++;
            size -= 8;
        }

        p1 = (const uint8_t*)q1;
        p2 = (const uint8_t*)q2;
    }

    // Final byte-by-byte comparison
    while (size > 0 && p1 && p2) {
        if (*p1 != *p2) {
            return *p1 < *p2 ? -1 : 1;
        }
        p1++;
        p2++;
        size--;
    }

    return 0;

}