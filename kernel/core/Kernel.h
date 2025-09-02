#ifndef KERNEL_H
#define KERNEL_H

#define asmlinkage __attribute__((regparm(0)))

#include "stdint.h"

void ParseMultibootInfo(uint32_t info);

#endif
