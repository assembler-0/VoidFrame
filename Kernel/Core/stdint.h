/*
 * stdint.h
 */

#ifndef VOIDFRAME_STDINT_H
#define VOIDFRAME_STDINT_H

#define NULL ((void*)0)
#define INT32_MAX ((int32_t)0x7FFFFFFF)
#define INT32_MIN ((int32_t)0x80000000)

typedef unsigned char uint8_t;
_Static_assert(sizeof(uint8_t) == 1, "sizeof(uint8_t) != 1");

typedef unsigned short uint16_t;
_Static_assert(sizeof(uint16_t) == 2, "sizeof(uint16_t) != 2");

typedef unsigned int uint32_t;
_Static_assert(sizeof(uint32_t) == 4, "sizeof(uint32_t) != 4");

typedef unsigned long long uint64_t;
_Static_assert(sizeof(uint64_t) == 8, "sizeof(uint64_t) != 8");

typedef unsigned char int8_t;
_Static_assert(sizeof(int8_t) == 1, "sizeof(int8_t) != 1");

typedef unsigned short int16_t;
_Static_assert(sizeof(int16_t) == 2, "sizeof(int16_t) != 2");

typedef unsigned int int32_t;
_Static_assert(sizeof(int32_t) == 4, "sizeof(int32_t) != 4");

typedef unsigned long long int64_t;
_Static_assert(sizeof(int64_t) == 8, "sizeof(int64_t) != 8");

#endif
