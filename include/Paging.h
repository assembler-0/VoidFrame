#pragma once
#include "stdint.h"
extern void JumpToKernelHigherHalf(uint64_t entry_point, uint64_t new_stack_top);