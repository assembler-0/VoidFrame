#pragma once

#include <stdint.h>
#include <Kernel.h>

asmlinkage void SwitchToHigherHalf(uint64_t pml4_phys_addr, uint64_t jump_to_addr, uint64_t stack);