/*
 * stdint.h
 */

#ifndef VOIDFRAME_STDINT_H
#define VOIDFRAME_STDINT_H

typedef unsigned char uint8_t;
_Static_assert(sizeof(uint8_t) == 1, "sizeof(uint8_t) != 1");

typedef unsigned short uint16_t;
_Static_assert(sizeof(uint16_t) == 2, "sizeof(uint16_t) != 2");

typedef unsigned int uint32_t;
_Static_assert(sizeof(uint32_t) == 4, "sizeof(uint32_t) != 4");

typedef unsigned long long uint64_t;
_Static_assert(sizeof(uint64_t) == 8, "sizeof(uint64_t) != 8");

#endif
