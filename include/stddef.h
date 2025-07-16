#ifndef STDDEF_H
#define STDDEF_H

#include <stdint.h>

#undef size_t
#define size_t uint64_t

#ifndef NULL
#define NULL ((void*)0)
#endif

#endif //STDDEF_H
