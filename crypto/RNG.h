#ifndef VOIDFRAME_RNG_H
#define VOIDFRAME_RNG_H
#include "stdint.h"

void rng_seed(uint64_t a, uint64_t b);
uint64_t xoroshiro128plus();

int rdrand_supported(void);

uint16_t rdrand16(void);
uint32_t rdrand32(void);
uint64_t rdrand64(void);

#endif //VOIDFRAME_RNG_H