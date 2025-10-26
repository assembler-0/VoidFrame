#include "RNG.h"
#include "Io.h"

static uint64_t s[2]; // state

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t xoroshiro128plus(void) {
    uint64_t s0 = s[0];
    uint64_t s1 = s[1];
    uint64_t result = s0 + s1;
    s1 ^= s0;
    s[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14);
    s[1] = rotl(s1, 36);
    return result;
}

void rng_seed(uint64_t a, uint64_t b) {
    s[0] ^= a;
    s[1] ^= b;
}

int rdrand_supported(void) {
    unsigned int eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    return (ecx >> 30) & 1;
}

uint16_t rdrand16(void) {
    uint16_t r;
    __asm__ volatile("rdrand %0" : "=r"(r));
    return r;
}

uint32_t rdrand32(void) {
    uint32_t r;
    __asm__ volatile("rdrand %0" : "=r"(r));
    return r;
}

uint64_t rdrand64(void) {
    uint64_t r;
    __asm__ volatile("rdrand %0" : "=r"(r));
    return r;
}
