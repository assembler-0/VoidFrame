#ifndef KERNEL_H
#define KERNEL_H

#ifndef asmlinkage
#define asmlinkage __attribute__((regparm(0)))
#endif

#include "stdint.h"

void ParseMultibootInfo(uint32_t info);

#endif
