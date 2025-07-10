#include "UserMode.h"
#include "Memory.h"
#include "Process.h"
#include "Gdt.h"
#include "Panic.h"

void JumpToUserMode(void (*user_function)(void)) {
    ASSERT(user_function != NULL);
    // Allocate user stack
    void* user_stack = AllocPage();
    if (!user_stack) {
        Panic("Failed to allocate user stack");
    }
    
    uint64_t user_stack_top = (uint64_t)user_stack + STACK_SIZE;
    
    // Jump to Ring 3
    asm volatile(
        "mov %0, %%rsp\n"           // Set user stack
        "pushq %1\n"                // SS (user data selector)
        "pushq %0\n"                // RSP (user stack)
        "pushfq\n"                  // RFLAGS
        "pushq %2\n"                // CS (user code selector)  
        "pushq %3\n"                // RIP (user function)
        "iretq\n"
        :
        : "r"(user_stack_top),
          "i"(USER_DATA_SELECTOR | 3),  // Ring 3
          "i"(USER_CODE_SELECTOR | 3),  // Ring 3
          "r"(user_function)
        : "memory"
    );
}

void CreateUserProcess(void (*user_function)(void)) {
    ASSERT(user_function != NULL);
    // Create process but mark it as user mode
    uint32_t pid = CreateProcess(user_function);
    if (pid > 0) {
        Process* proc = GetProcessByPid(pid);
        if (proc) {
            proc->is_user_mode = 1;
        }
    }
}