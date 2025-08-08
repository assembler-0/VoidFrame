#include "Editor.h"
#include "Console.h"
#include "Fs.h"
#include "Keyboard.h"
#include "MemOps.h"

#define MAX_LINES 50
#define MAX_LINE_LEN 80

static char editor_buffer[MAX_LINES][MAX_LINE_LEN];
static int current_line = 0;
static int current_col = 0;
static int total_lines = 1;
static char filename[128];

static void EditorRefresh(void) {
    ClearScreen();
    PrintKernel("VoidFrame Editor - ");
    PrintKernel(filename);
    PrintKernel(" (Ctrl+S=Save, Ctrl+Q=Quit)\n");
    PrintKernel("----------------------------------------\n");
    
    for (int i = 0; i < total_lines && i < MAX_LINES; i++) {
        if (i == current_line) {
            PrintKernel("> ");
        } else {
            PrintKernel("  ");
        }
        PrintKernel(editor_buffer[i]);
        PrintKernel("\n");
    }
    
    PrintKernel("----------------------------------------\n");
    PrintKernel("Line ");
    PrintKernelInt(current_line + 1);
    PrintKernel("/");
    PrintKernelInt(total_lines);
    PrintKernel(" Col ");
    PrintKernelInt(current_col + 1);
}

static void EditorSave(void) {
    int fd = FsOpen(filename, FS_WRITE);
    if (fd < 0) {
        PrintKernel("\nError: Cannot save file\n");
        return;
    }
    
    for (int i = 0; i < total_lines; i++) {
        int len = 0;
        while (editor_buffer[i][len]) len++;
        if (len > 0) {
            FsWrite(fd, editor_buffer[i], len);
        }
        if (i < total_lines - 1) {
            FsWrite(fd, "\n", 1);
        }
    }
    
    FsClose(fd);
    PrintKernel("\nFile saved!\n");
}

static void EditorLoad(void) {
    int fd = FsOpen(filename, FS_READ);
    if (fd < 0) {
        // New file
        FastMemset(editor_buffer, 0, sizeof(editor_buffer));
        total_lines = 1;
        current_line = 0;
        current_col = 0;
        return;
    }
    
    char buffer[1024];
    int bytes_read = FsRead(fd, buffer, sizeof(buffer) - 1);
    FsClose(fd);
    
    if (bytes_read <= 0) {
        FastMemset(editor_buffer, 0, sizeof(editor_buffer));
        total_lines = 1;
        current_line = 0;
        current_col = 0;
        return;
    }
    
    buffer[bytes_read] = 0;
    FastMemset(editor_buffer, 0, sizeof(editor_buffer));
    
    int line = 0, col = 0;
    for (int i = 0; i < bytes_read && line < MAX_LINES; i++) {
        if (buffer[i] == '\n') {
            line++;
            col = 0;
        } else if (col < MAX_LINE_LEN - 1) {
            editor_buffer[line][col++] = buffer[i];
        }
    }
    
    total_lines = line + 1;
    current_line = 0;
    current_col = 0;
}

void EditorOpen(const char* file) {
    if (!file) return;
    
    int len = 0;
    while (file[len] && len < 127) len++;
    FastMemcpy(filename, file, len);
    filename[len] = '\0';
    
    EditorLoad();
    
    while (1) {
        EditorRefresh();
        
        while (!HasInput()) {
            // Wait for input
        }
        
        char c = GetChar();
        
        if (c == 19) { // Ctrl+S
            EditorSave();
            continue;
        }
        
        if (c == 17) { // Ctrl+Q
            PrintKernel("\nExiting editor...\n");
            break;
        }
        
        if (c == '\n') {
            // New line
            if (total_lines < MAX_LINES) {
                // Move lines down
                for (int i = total_lines; i > current_line + 1; i--) {
                    FastMemcpy(editor_buffer[i], editor_buffer[i-1], MAX_LINE_LEN);
                }
                // Clear new line
                FastMemset(editor_buffer[current_line + 1], 0, MAX_LINE_LEN);
                current_line++;
                current_col = 0;
                total_lines++;
            }
        } else if (c == '\b') {
            // Backspace
            if (current_col > 0) {
                current_col--;
                // Shift characters left
                for (int i = current_col; i < MAX_LINE_LEN - 1; i++) {
                    editor_buffer[current_line][i] = editor_buffer[current_line][i + 1];
                }
            } else if (current_line > 0) {
                // Join with previous line
                int prev_len = 0;
                while (editor_buffer[current_line - 1][prev_len] && prev_len < MAX_LINE_LEN - 1) {
                    prev_len++;
                }
                
                // Copy current line to end of previous (with bounds check)
                for (int i = 0; i < MAX_LINE_LEN - prev_len - 1 && prev_len + i < MAX_LINE_LEN - 1; i++) {
                    if (editor_buffer[current_line][i] == 0) break;
                    editor_buffer[current_line - 1][prev_len + i] = editor_buffer[current_line][i];
                }
                
                // Move lines up
                for (int i = current_line; i < total_lines - 1; i++) {
                    FastMemcpy(editor_buffer[i], editor_buffer[i + 1], MAX_LINE_LEN);
                }
                
                current_line--;
                current_col = prev_len;
                total_lines--;
            }
        } else if (c >= 32 && c <= 126) {
            // Printable character
            if (current_col < MAX_LINE_LEN - 1) {
                // Shift characters right
                for (int i = MAX_LINE_LEN - 2; i > current_col; i--) {
                    editor_buffer[current_line][i] = editor_buffer[current_line][i - 1];
                }
                editor_buffer[current_line][current_col] = c;
                current_col++;
            }
        }
        
        // Navigation with Ctrl keys
        if (c == 23) { // Ctrl+W - Up
            if (current_line > 0) {
                current_line--;
                if (current_col >= MAX_LINE_LEN) current_col = MAX_LINE_LEN - 1;
            }
        } else if (c == 1) { // Ctrl+A - Left
            if (current_col > 0) current_col--;
        } else if (c == 4) { // Ctrl+D - Right
            if (current_col < MAX_LINE_LEN - 1 && editor_buffer[current_line][current_col]) {
                current_col++;
            }
        } else if (c == 24) { // Ctrl+X - Down
            if (current_line < total_lines - 1) {
                current_line++;
                if (current_col >= MAX_LINE_LEN) current_col = MAX_LINE_LEN - 1;
            }
        }
    }
}