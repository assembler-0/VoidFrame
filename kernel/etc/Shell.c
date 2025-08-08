#include "Shell.h"
#include "Console.h"
#include "Fs.h"
#include "FsUtils.h"
#include "Keyboard.h"
#include "MemOps.h"
#include "Process.h"

static char command_buffer[256];
static int cmd_pos = 0;

static void Version() {
    PrintKernelSuccess("VoidFrame v0.0.1-beta\n");
    PrintKernelSuccess("VoidFrame Shell v0.0.1-beta\n");
}

static char* GetArg(const char* cmd, int arg_num) {
    static char arg_buf[64];
    int word = 0, pos = 0, buf_pos = 0;
    
    while (cmd[pos] && word <= arg_num) {
        if (cmd[pos] == ' ') {
            if (word == arg_num && buf_pos > 0) {
                arg_buf[buf_pos] = 0;
                return arg_buf;
            }
            while (cmd[pos] == ' ') pos++;
            word++;
            buf_pos = 0;
        } else {
            if (word == arg_num && buf_pos < 63) {
                arg_buf[buf_pos++] = cmd[pos];
            }
            pos++;
        }
    }
    
    if (word == arg_num && buf_pos > 0) {
        arg_buf[buf_pos] = 0;
        return arg_buf;
    }
    return NULL;
}

static void ExecuteCommand(const char* cmd) {
    char* cmd_name = GetArg(cmd, 0);
    if (!cmd_name) return;
    
    if (FastStrCmp(cmd_name, "help") == 0) {
        PrintKernel("VoidFrame Shell Commands:\n");
        PrintKernel("  help           - Show this help\n");
        PrintKernel("  ps             - List processes\n");
        PrintKernel("  sched          - Show scheduler state\n");
        PrintKernel("  perf           - Show performance stats\n");
        PrintKernel("  clear          - Clear screen\n");
        PrintKernel("  ls [path]      - List directory contents\n");
        PrintKernel("  cat <file>     - Display file contents\n");
        PrintKernel("  mkdir <name>   - Create directory\n");
        PrintKernel("  touch <name>   - Create empty file\n");
        PrintKernel("  echo <text> <file> - Write text to file\n");
        PrintKernel("  write <file>   - Interactive file editor\n");
        PrintKernel("  fstest         - Run filesystem tests\n");
    } else if (FastStrCmp(cmd_name, "ps") == 0) {
        ListProcesses();
    } else if (FastStrCmp(cmd_name, "perf") == 0) {
        DumpPerformanceStats();
    } else if (FastStrCmp(cmd_name, "ver") == 0) {
        Version();
    } else if (FastStrCmp(cmd_name, "sched") == 0) {
        DumpSchedulerState();
    } else if (FastStrCmp(cmd_name, "clear") == 0) {
        ClearScreen();
    } else if (FastStrCmp(cmd_name, "ls") == 0) {
        char* path = GetArg(cmd, 1);
        FsLs(path ? path : "/");
    } else if (FastStrCmp(cmd_name, "cat") == 0) {
        char* file = GetArg(cmd, 1);
        if (file) {
            char full_path[128];
            if (file[0] != '/') {
                full_path[0] = '/';
                int len = 0;
                while (file[len] && len < 126) len++;
                FastMemcpy(full_path + 1, file, len);
                full_path[len + 1] = '\0';
            } else {
                int len = 0;
                while (file[len] && len < 127) len++;
                FastMemcpy(full_path, file, len);
                full_path[len] = '\0';
            }
            FsCat(full_path);
        } else {
            PrintKernel("Usage: cat <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "mkdir") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[128];
            if (name[0] != '/') {
                full_path[0] = '/';
                int len = 0;
                while (name[len] && len < 126) len++;
                FastMemcpy(full_path + 1, name, len);
                full_path[len + 1] = '\0';
            } else {
                int len = 0;
                while (name[len] && len < 127) len++;
                FastMemcpy(full_path, name, len);
                full_path[len] = '\0';
            }
            if (FsMkdir(full_path) == 0) {
                PrintKernel("Directory created\n");
            } else {
                PrintKernel("Failed to create directory\n");
            }
        } else {
            PrintKernel("Usage: mkdir <dirname>\n");
        }
    } else if (FastStrCmp(cmd_name, "touch") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[128];
            if (name[0] != '/') {
                full_path[0] = '/';
                int len = 0;
                while (name[len] && len < 126) len++;
                FastMemcpy(full_path + 1, name, len);
                full_path[len + 1] = '\0';
            } else {
                int len = 0;
                while (name[len] && len < 127) len++;
                FastMemcpy(full_path, name, len);
                full_path[len] = '\0';
            }
            if (FsTouch(full_path) == 0) {
                PrintKernel("File created\n");
            } else {
                PrintKernel("Failed to create file\n");
            }
        } else {
            PrintKernel("Usage: touch <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "echo") == 0) {
        char* text = GetArg(cmd, 1);
        char* file = GetArg(cmd, 2);
        if (text && file) {
            char full_path[128];
            if (file[0] != '/') {
                full_path[0] = '/';
                int len = 0;
                while (file[len] && len < 126) len++;
                FastMemcpy(full_path + 1, file, len);
                full_path[len + 1] = '\0';
            } else {
                int len = 0;
                while (file[len] && len < 127) len++;
                FastMemcpy(full_path, file, len);
                full_path[len] = '\0';
            }
            if (FsEcho(text, full_path) == 0) {
                PrintKernel("Text written to file\n");
            } else {
                PrintKernel("Failed to write to file\n");
            }
        } else {
            PrintKernel("Usage: echo <text> <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "write") == 0) {
        char* file = GetArg(cmd, 1);
        if (file) {
            char full_path[128];
            if (file[0] != '/') {
                full_path[0] = '/';
                int len = 0;
                while (file[len] && len < 126) len++;
                FastMemcpy(full_path + 1, file, len);
                full_path[len + 1] = '\0';
            } else {
                int len = 0;
                while (file[len] && len < 127) len++;
                FastMemcpy(full_path, file, len);
                full_path[len] = '\0';
            }
            
            PrintKernel("Writing to ");
            PrintKernel(full_path);
            PrintKernel(" (Ctrl+D to save and exit)\n");
            
            int fd = FsOpen(full_path, FS_WRITE);
            if (fd < 0) {
                PrintKernel("Failed to open file\n");
                return;
            }
            
            char write_buffer[1024];
            int write_pos = 0;
            
            while (1) {
                if (HasInput()) {
                    char c = GetChar();
                    
                    if (c == 4) { // Ctrl+D
                        if (write_pos > 0) {
                            FsWrite(fd, write_buffer, write_pos);
                        }
                        FsClose(fd);
                        PrintKernel("\nFile saved\n");
                        break;
                    } else if (c == '\n') {
                        write_buffer[write_pos++] = '\n';
                        PrintKernel("\n");
                        if (write_pos >= 1023) {
                            FsWrite(fd, write_buffer, write_pos);
                            write_pos = 0;
                        }
                    } else if (c == '\b') {
                        if (write_pos > 0) {
                            write_pos--;
                            PrintKernel("\b \b");
                        }
                    } else if (write_pos < 1023) {
                        write_buffer[write_pos++] = c;
                        char str[2] = {c, 0};
                        PrintKernel(str);
                    }
                } else {
                    Yield();
                }
            }
        } else {
            PrintKernel("Usage: write <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "fstest") == 0) {
        FsTest();
    } else {
        PrintKernel("Unknown command: ");
        PrintKernel(cmd_name);
        PrintKernel("\nType 'help' for commands\n");
    }
}

void ShellInit(void) {
    cmd_pos = 0;
}

void ShellProcess(void) {
    PrintKernelSuccess("VoidFrame Shell v0.0.1-beta\n");
    while (1) {
        if (HasInput()) {
            char c = GetChar();

            if (c == '\n') {
                PrintKernel("\n"); // Move to next line
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
