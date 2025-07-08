#include "Shell.h"
#include "stdint.h"
#include "Kernel.h"
#include "../Drivers/Driver.h"
#include "../Memory/Memory.h"
#include "../Process/Process.h"
#include "../Core/Panic.h"
static char command_buffer[256];
static int cmd_pos = 0;

// String compare
static int strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a - *b;
}

// String copy
static void strcpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = 0;
}

// Execute command
static void ExecuteCommand(const char* cmd) {
    if (strcmp(cmd, "help") == 0) {
        PrintKernel("VoidFrame Shell Commands:\n");
        PrintKernel("  help    - Show this help\n");
        PrintKernel("  clear   - Clear screen\n");
        PrintKernel("  mem     - Show memory info\n");
        PrintKernel("  proc    - Show process info\n");
        PrintKernel("  reboot  - Restart system\n");
        
    } else if (strcmp(cmd, "clear") == 0) {
        ClearScreen();
        CurrentLine = 0;
        CurrentColumn = 0;
        PrintKernel("VoidFrame Shell v0.1\n");
        
    } else if (strcmp(cmd, "mem") == 0) {
        uint64_t free = GetFreeMemory();
        PrintKernel("Free Memory: ");
        PrintKernelHex(free);
        PrintKernel(" bytes\n");
        
    } else if (strcmp(cmd, "proc") == 0) {
        PrintKernel("Active processes: 1 (kernel tasks + user)\n");
        PrintKernel("Scheduler: 4000Hz preemptive\n");
        
    } else if (strcmp(cmd, "crash") == 0) {
        PrintKernel("Calling Panic()\n");
        Panic("Panic() called from system");

    } else if (strcmp(cmd, "reboot") == 0) {
        PrintKernel("Rebooting...\n");
        asm volatile("cli; hlt");
        
    } else if (cmd[0] != 0) {
        PrintKernel("Unknown command: ");
        PrintKernel(cmd);
        PrintKernel("\nType 'help' for commands\n");
    }
}

void ShellInit(void) {
    PrintKernel("\nVoidFrame Shell v0.1\n");
    PrintKernel("Type 'help' for commands\n");
    PrintKernel("$ ");
}

void ShellRun(void) {
    Driver* kbd = DriverGet(DRIVER_KEYBOARD);
    if (!kbd) return;
    
    char input[64];
    int bytes = kbd->read(input, sizeof(input));
    
    for (int i = 0; i < bytes; i++) {
        char c = input[i];
        
        if (c == '\n') {
            // Execute command
            command_buffer[cmd_pos] = 0;
            PrintKernel("\n");
            ExecuteCommand(command_buffer);
            PrintKernel("$ ");
            cmd_pos = 0;
            
        } else if (c == '\b') {
            if (cmd_pos > 0) cmd_pos--;
            
        } else if (c >= 32 && c < 127) {
            // Printable character
            if (cmd_pos < 255) {
                command_buffer[cmd_pos++] = c;
            }
        }
    }
}