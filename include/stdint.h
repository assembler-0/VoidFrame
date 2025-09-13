#ifndef VOIDFRAME_STDINT_H
#define VOIDFRAME_STDINT_H

#define NULL ((void*)0)
#define INT32_MAX ((int32_t)0x7FFFFFFF)
#define INT32_MIN ((int32_t)0x80000000)
#define UINT32_MAX ((uint32_t)0xFFFFFFFF)
#define UINT32_MIN ((uint32_t)0x00000000)
#define INT64_MAX ((int64_t)0x7FFFFFFFFFFFFFFF)
#define INT64_MIN ((int64_t)0x8000000000000000)
#define UINT64_MAX ((uint64_t)0xFFFFFFFFFFFFFFFF)
#define UINT64_MIN ((uint64_t)0x0000000000000000)

typedef unsigned char uint8_t;
_Static_assert(sizeof(uint8_t) == 1, "sizeof(uint8_t) != 1");

typedef unsigned short uint16_t;
_Static_assert(sizeof(uint16_t) == 2, "sizeof(uint16_t) != 2");

typedef unsigned int uint32_t;
_Static_assert(sizeof(uint32_t) == 4, "sizeof(uint32_t) != 4");

typedef unsigned int size_t;
_Static_assert(sizeof(size_t) == 4, "sizeof(uint32_t) != 4");

typedef unsigned long long uint64_t;
_Static_assert(sizeof(uint64_t) == 8, "sizeof(uint64_t) != 8");

typedef unsigned long long uintptr_t;
_Static_assert(sizeof(uintptr_t) == 8, "sizeof(uintptr_t) != 8");

typedef signed char int8_t;
_Static_assert(sizeof(int8_t) == 1, "sizeof(int8_t) != 1");

typedef signed short int16_t;
_Static_assert(sizeof(int16_t) == 2, "sizeof(int16_t) != 2");

typedef signed int int32_t;
_Static_assert(sizeof(int32_t) == 4, "sizeof(int32_t) != 4");

typedef signed long long int64_t;
_Static_assert(sizeof(int64_t) == 8, "sizeof(int64_t) != 8");

typedef signed long intptr_t;
_Static_assert(sizeof(intptr_t) == 8, "sizeof(intptr_t) != 8");

typedef __int128_t int128_t;
_Static_assert(sizeof(int128_t) == 16, "sizeof(int128_t) != 16");

#endif
