#include <StackGuard.h>
#include <Panic.h>

uint64_t __stack_chk_guard = STACK_CANARY_VALUE;

void __stack_chk_fail(void) {
    PANIC("Stack overflow detected!");
}

void StackGuardInit(void) {
    // Keep canary value consistent - don't randomize it
    // Cerberus expects STACK_CANARY_VALUE to be constant
    __stack_chk_guard = STACK_CANARY_VALUE;
    PrintKernelSuccess("StackGuard initialized with fixed canary\n");
}