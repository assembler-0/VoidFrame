#include "Shell.h"
#include "Console.h"
#include "Editor.h"
#include "Fs.h"
#include "FsUtils.h"
#include "Keyboard.h"
#include "MemOps.h"
#include "Process.h"
#include "StringOps.h"

static char command_buffer[256];
static int cmd_pos = 0;
static char current_dir[256] = "/";

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

static void ResolvePath(const char* input, char* output, int max_len) {
    if (!input || !output) return;
    
    if (input[0] == '/') {
        // Absolute path
        int len = 0;
        while (input[len] && len < max_len - 1) {
            output[len] = input[len];
            len++;
        }
        output[len] = '\0';
    } else {
        // Relative path - combine with current directory
        int curr_len = 0;
        while (current_dir[curr_len] && curr_len < max_len - 1) {
            output[curr_len] = current_dir[curr_len];
            curr_len++;
        }
        
        if (curr_len > 0 && current_dir[curr_len - 1] != '/' && curr_len < max_len - 1) {
            output[curr_len++] = '/';
        }
        
        int input_len = 0;
        while (input[input_len] && curr_len + input_len < max_len - 1) {
            output[curr_len + input_len] = input[input_len];
            input_len++;
        }
        output[curr_len + input_len] = '\0';
    }
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
        PrintKernel("  cd <dir>       - Change directory\n");
        PrintKernel("  pwd            - Show current directory\n");
        PrintKernel("  ls [path]      - List directory contents\n");
        PrintKernel("  cat <file>     - Display file contents\n");
        PrintKernel("  mkdir <name>   - Create directory\n");
        PrintKernel("  touch <name>   - Create empty file\n");
        PrintKernel("  rm <file>      - Remove file or empty directory\n");
        PrintKernel("  echo <text> <file> - Write text to file\n");
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
    } else if (FastStrCmp(cmd_name, "cd") == 0) {
        char* dir = GetArg(cmd, 1);
        if (!dir) {
            FastMemcpy(current_dir, "/", 2);
            PrintKernel("[VFRFS] DIRECTORY SWITCHED TO /\n");
        } else {
            char new_path[256];
            if (dir[0] == '/') {
                // Absolute path
                int len = 0;
                while (dir[len] && len < 255) len++;
                FastMemcpy(new_path, dir, len);
                new_path[len] = '\0';
            } else {
                // Relative path
                int curr_len = 0;
                while (current_dir[curr_len]) curr_len++;
                
                FastMemcpy(new_path, current_dir, curr_len);
                if (current_dir[curr_len - 1] != '/') {
                    new_path[curr_len++] = '/';
                }
                
                int dir_len = 0;
                while (dir[dir_len] && curr_len + dir_len < 255) {
                    new_path[curr_len + dir_len] = dir[dir_len];
                    dir_len++;
                }
                new_path[curr_len + dir_len] = '\0';
            }
            
            FsNode* target = FsFind(new_path);
            if (!target) {
                PrintKernel("cd: directory not found\n");
            } else if (target->type != FS_DIRECTORY) {
                PrintKernel("cd: not a directory\n");
            } else {
                FastMemcpy(current_dir, new_path, 256);
                PrintKernel("[VFRFS] DIRECTORY SWITCHED TO ");
                PrintKernel(current_dir);
                PrintKernel("\n");
            }
        }
    } else if (FastStrCmp(cmd_name, "pwd") == 0) {
        PrintKernel(current_dir);
        PrintKernel("\n");
    } else if (FastStrCmp(cmd_name, "ls") == 0) {
        char* path = GetArg(cmd, 1);
        if (!path) {
            FsLs(current_dir);
        } else if (path[0] == '/') {
            FsLs(path);
        } else {
            char full_path[256];
            int curr_len = 0;
            while (current_dir[curr_len]) curr_len++;
            
            FastMemcpy(full_path, current_dir, curr_len);
            if (current_dir[curr_len - 1] != '/') {
                full_path[curr_len++] = '/';
            }
            
            int path_len = 0;
            while (path[path_len] && curr_len + path_len < 255) {
                full_path[curr_len + path_len] = path[path_len];
                path_len++;
            }
            full_path[curr_len + path_len] = '\0';
            FsLs(full_path);
        }
    } else if (FastStrCmp(cmd_name, "cat") == 0) {
        char* file = GetArg(cmd, 1);
        if (file) {
            char full_path[256];
            ResolvePath(file, full_path, 256);
            FsCat(full_path);
        } else {
            PrintKernel("Usage: cat <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "mkdir") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[256];
            ResolvePath(name, full_path, 256);
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
            char full_path[256];
            ResolvePath(name, full_path, 256);
            if (FsTouch(full_path) == 0) {
                PrintKernel("File created\n");
            } else {
                PrintKernel("Failed to create file\n");
            }
        } else {
            PrintKernel("Usage: touch <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "rm") == 0) {
        char* name = GetArg(cmd, 1);
        if (name) {
            char full_path[256];
            ResolvePath(name, full_path, 256);
            if (FsDelete(full_path) == 0) {
                PrintKernel("Removed\n");
            } else {
                PrintKernel("Failed to remove (file not found or directory not empty)\n");
            }
        } else {
            PrintKernel("Usage: rm <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "echo") == 0) {
        char* text = GetArg(cmd, 1);
        char* file = GetArg(cmd, 2);
        if (text && file) {
            char full_path[256];
            ResolvePath(file, full_path, 256);
            if (FsEcho(text, full_path) == 0) {
                PrintKernel("Text written to file\n");
            } else {
                PrintKernel("Failed to write to file\n");
            }
        } else {
            PrintKernel("Usage: echo <text> <filename>\n");
        }
    } else if (FastStrCmp(cmd_name, "edit") == 0) {
        char* file = GetArg(cmd, 1);
        if (file) {
            char full_path[256];
            ResolvePath(file, full_path, 256);
            EditorOpen(full_path);
        } else {
            PrintKernel("Usage: edit <filename>\n");
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
    ClearScreen();
    PrintKernelSuccess("VoidFrame Shell. Press ENTER to start shell\n");
    while (1) {
        if (HasInput()) {
            char c = GetChar();

            if (c == '\n') {
                PrintKernel("\n"); // Move to next line
                command_buffer[cmd_pos] = 0;
                ExecuteCommand(command_buffer);
                cmd_pos = 0;
                PrintKernel(current_dir);
                PrintKernel("> ");
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
