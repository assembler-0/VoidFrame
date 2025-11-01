#include "MemOps.h"
#include "Io.h"
#include "Panic.h"
#include "x64.h"

extern void* memcpy_internal_sse2(void* restrict dest, const void* restrict src, uint64_t size);
extern void* memcpy_internal_avx2(void* restrict dest, const void* restrict src, uint64_t size);
extern void* memcpy_internal_avx512(void* restrict dest, const void* restrict src, uint64_t size);

extern void* memcpy_internal_sse2_wc(void* restrict dest, const void* restrict src, uint64_t size);
extern void* memcpy_internal_avx2_wc(void* restrict dest, const void* restrict src, uint64_t size);
extern void* memcpy_internal_avx512_wc(void* restrict dest, const void* restrict src, uint64_t size);

extern void* memset_internal_sse2(void* restrict dest, int value, uint64_t size);
extern void* memset_internal_avx2(void* restrict dest, int value, uint64_t size);
extern void* memset_internal_avx512(void* restrict dest, int value, uint64_t size);

extern int memcmp_internal_sse2(const void* restrict s1, const void* restrict s2, uint64_t size);
extern int memcmp_internal_avx2(const void* restrict s1, const void* restrict s2, uint64_t size);
extern int memcmp_internal_avx512(const void* restrict s1, const void* restrict s2, uint64_t size);

extern void zeropage_internal_sse2(void* restrict page);
extern void zeropage_internal_avx2(void* restrict page);
extern void zeropage_internal_avx512(void* restrict page);

void* memset(void* restrict dest, const int value, const unsigned long size) {
    return FastMemset(dest, value, size);
}

void* memcpy(void* restrict dest, const void* restrict src, const unsigned long size) {
    return FastMemcpy(dest, src, size);
}

int memcmp(const void* restrict s1, const void* restrict s2, const unsigned long size) {
    return FastMemcmp(s1, s2, size);
}

void* FastMemset(void* restrict dest, const int value, uint64_t size) {
    ASSERT(dest != NULL);

    if (size == 0) return dest;

    const CpuFeatures * features = GetCpuFeatures();
    uint8_t* d = dest;
    const uint8_t val = (uint8_t)value;

    if (features->avx512f) return memset_internal_avx512(d, value, size);
    if (features->avx2) return memset_internal_avx2(d, value, size);
    if (features->sse2) return memset_internal_sse2(d, value, size);

    while (size--) *d++ = val;
    return dest;
}

void* FastMemcpy(void* restrict dest, const void* restrict src, uint64_t size) {
    ASSERT(dest != NULL && src != NULL);

    if (size == 0) return dest;

    uint8_t* d = dest;
    const uint8_t* s = src;

    if (d == s || !d || !s) return dest;

    const CpuFeatures * features = GetCpuFeatures();

#ifdef VF_CONFIG_MEMCPY_NT
    if (features->avx512f) return memcpy_internal_avx512(d, s, size);
    if (features->avx2) return memcpy_internal_avx2(d, s, size);
    if (features->sse2) return memcpy_internal_sse2(d, s, size);
#else
    if (features->avx512f) return memcpy_internal_avx512_wc(d, s, size);
    if (features->avx2) return memcpy_internal_avx2_wc(d, s, size);
    if (features->sse2) return memcpy_internal_sse2_wc(d, s, size);
#endif

    while (size--) *d++ = *s++;

    return dest;
}

void FastZeroPage(void* restrict page) {
    ASSERT(page != NULL);
    const CpuFeatures * features = GetCpuFeatures();

    if (features->avx512f) zeropage_internal_avx512(page);
    else if (features->avx2) zeropage_internal_avx2(page);
    else if (features->sse2) zeropage_internal_sse2(page);
    else FastMemset(page, 0, PAGE_SIZE);
}

int FastMemcmp(const void* restrict ptr1, const void* restrict ptr2, uint64_t size) {
    if (size == 0) return 0;
    ASSERT(ptr1 != NULL && ptr2 != NULL);

    const uint8_t* p1 = ptr1;
    const uint8_t* p2 = ptr2;
    const CpuFeatures* features = GetCpuFeatures();

    if (features->avx512f) return memcmp_internal_avx512(p1, p2, size);
    if (features->avx2) return memcmp_internal_avx2(p1, p2, size);
    if (features->sse2) return memcmp_internal_sse2(p1, p2, size);

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