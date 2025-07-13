#include "MemOps.h"
#include "Cpu.h"
#include "Panic.h"

void* FastMemset(void* dest, int value, uint64_t size) {
    ASSERT(dest != NULL);
    CpuFeatures* features = GetCpuFeatures();
    uint8_t* d = (uint8_t*)dest;
    
    if (features->sse2 && size >= 16) {
        // Use SSE2 for large blocks
        uint64_t val64 = ((uint64_t)value << 56) | ((uint64_t)value << 48) | 
                        ((uint64_t)value << 40) | ((uint64_t)value << 32) |
                        ((uint64_t)value << 24) | ((uint64_t)value << 16) |
                        ((uint64_t)value << 8) | value;
        
        asm volatile(
            "movq %0, %%xmm0\n"
            "movq %0, %%xmm1\n"
            "punpcklqdq %%xmm1, %%xmm0\n"
            :
            : "r"(val64)
            : "xmm0", "xmm1"
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
    CpuFeatures* features = GetCpuFeatures();
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (features->sse2 && size >= 16) {
        while (size >= 16) {
            asm volatile(
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
    
    while (size--) *d++ = *s++;
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