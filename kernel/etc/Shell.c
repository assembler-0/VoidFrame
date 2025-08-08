#include "Shell.h"
#include "Console.h"
#include "Keyboard.h"
#include "MemOps.h"
#include "Process.h"

static char command_buffer[256];
static int cmd_pos = 0;

static void Version() {
    PrintKernelSuccess("\nVoidFrame v0.0.1-beta\n");
    PrintKernelSuccess("VFS v0.0.1-beta\n");
}

static void ExecuteCommand(const char* cmd) {
    if (FastStrCmp(cmd, "help") == 0) {
        PrintKernel("\nVoidFrame Shell Commands:\n");
        PrintKernel("  help     - Show this help\n");
        PrintKernel("  ps       - List processes\n");
        PrintKernel("  sched    - Show scheduler state\n");
        PrintKernel("  perf     - Show performance stats\n");
        PrintKernel("  clear    - Clear screen\n");
    } else if (FastStrCmp(cmd, "ps") == 0) {
        ListProcesses();
    } else if (FastStrCmp(cmd, "perf") == 0) {
        DumpPerformanceStats();
    } else if (FastStrCmp(cmd, "ver") == 0) {
        Version();
    } else if (FastStrCmp(cmd, "sched") == 0) {
        DumpSchedulerState();
    } else if (FastStrCmp(cmd, "clear") == 0) {
        ClearScreen();
    } else if (cmd[0] != 0) {
        PrintKernel("\nUnknown command: ");
        PrintKernel(cmd);
        PrintKernel("\nType 'help' for commands\n");
    }
}

void ShellInit(void) {
    cmd_pos = 0;
}

void ShellProcess(void) {
    while (1) {
        if (HasInput()) {
            char c = GetChar();

            if (c == '\n') {
                command_buffer[cmd_pos] = 0;
                ExecuteCommand(command_buffer);
                cmd_pos = 0;
                PrintKernel("VFS> ");
            } else if (c == '\b') {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    PrintKernel("\b \b"); // Visual backspace
                }
            } else if (cmd_pos < 255) {
                command_buffer[cmd_pos++] = c;
                char str[2] = {c, 0};
                PrintKernel(str); // Echo character
            }
        } else {
            // Yield CPU when no input available
            Yield();
        }
    }
}
