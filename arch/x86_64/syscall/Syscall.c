#include "Syscall.h"
#include "Console.h"
#include "MLFQ.h"

uint64_t SyscallHandler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    switch (syscall_num) {
        case SYS_EXIT:
            MLFQKillCurrentProcess("SYS_EXIT");
            MLFQYield();
            return arg1;

        case SYS_WRITE:
            // arg1 = fd (ignored for now), arg2 = buffer, arg3 = count
            if (arg1 == 1) { // stdout
                const char* buffer = (char*)arg2;
                PrintKernel(buffer);
                return arg3;
            }
            return -1;

        case SYS_READ:

            return 0;


        default:
            return -1;
    }
}
