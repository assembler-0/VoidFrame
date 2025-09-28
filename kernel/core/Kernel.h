#ifndef KERNEL_H
#define KERNEL_H


#ifndef asmlinkage
# if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#  define asmlinkage __attribute__((regparm(0)))
# else
#  define asmlinkage extern
# endif
#endif

#include "stdint.h"

void ParseMultibootInfo(uint32_t info);

#endif
