#ifndef STACK_GUARD_H
#define STACK_GUARD_H

#include "stdint.h"

#define STACK_CANARY_VALUE 0xDEADBEEFCAFEBABE

extern uint64_t __stack_chk_guard;

void __stack_chk_fail(void);
void StackGuardInit(void);

#endif