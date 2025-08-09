#include "Editor.h"
#include "Console.h"
#include "VFS.h"
#include "Keyboard.h"
#include "MemOps.h"
#include "Process.h"
#include "StringOps.h"
#include "KernelHeap.h"

#define MAX_LINES 50
#define MAX_LINE_LEN 80
#define FILENAME_MAX_LEN 127

static char editor_buffer[MAX_LINES][MAX_LINE_LEN];
static int current_line = 0;
static int current_col = 0;
static int total_lines = 1;
static char filename[FILENAME_MAX_LEN + 1];
static int dirty = 0; // Track if file has unsaved changes
static int goto_mode = 0; // Track if we're in goto line mode
static char goto_buffer[10]; // Buffer for line number input
static int goto_pos = 0; // Position in goto buffer

// Helper function to get line length
static int get_line_length(int line) {
    if (line < 0 || line >= MAX_LINES) return 0;
    return FastStrlen(editor_buffer[line], MAX_LINE_LEN);
}

// Helper function to clamp cursor position
static void clamp_cursor(void) {
    if (current_line < 0) current_line = 0;
    if (current_line >= total_lines) current_line = total_lines - 1;
    if (current_line >= MAX_LINES) current_line = MAX_LINES - 1;

    int line_len = get_line_length(current_line);
    if (current_col > line_len) current_col = line_len;
    if (current_col < 0) current_col = 0;
    if (current_col >= MAX_LINE_LEN) current_col = MAX_LINE_LEN - 1;
}

static void EditorRefresh(void) {
    ClearScreen();
    PrintKernel("VoidFrame Editor - ");
    PrintKernel(filename);
    if (dirty) {
        PrintKernel(" *");
    }
    PrintKernel(" (Ctrl+S=Save, Ctrl+Q=Quit, Ctrl+L=Goto Line)\n");

    if (goto_mode) {
        PrintKernel("Go to line: ");
        PrintKernel(goto_buffer);
        PrintKernel("_\n");
        PrintKernel("(Enter to go, Esc to cancel)\n");
        PrintKernel("----------------------------------------\n");
        return;
    }

    PrintKernel("----------------------------------------\n");

    // Display visible lines (simple scrolling)
    int start_line = 0;
    int display_lines = 20; // Adjust based on your screen size

    if (current_line >= display_lines) {
        start_line = current_line - display_lines + 1;
    }

    for (int i = start_line; i < start_line + display_lines && i < total_lines && i < MAX_LINES; i++) {
        if (i == current_line) {
            PrintKernel("> ");
        } else {
            PrintKernel("  ");
        }

        // Print line number for reference
        if (i < 9) PrintKernel(" ");
        PrintKernelInt(i + 1);
        PrintKernel(": ");

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
    if (dirty) {
        PrintKernel(" [Modified]");
    }
    PrintKernel("\n");
}

static int EditorSave(void) {
    // Compute total length
    int total_len = 0;
    for (int i = 0; i < total_lines && i < MAX_LINES; i++) {
        total_len += get_line_length(i);
        if (i < total_lines - 1) total_len += 1; // newline
    }

    char* buffer = (char*)KernelMemoryAlloc(total_len > 0 ? (size_t)total_len : 1);
    if (!buffer) {
        PrintKernel("\nError: Out of memory\n");
        return 0;
    }

    int pos = 0;
    for (int i = 0; i < total_lines && i < MAX_LINES; i++) {
        int len = get_line_length(i);
        if (len > 0) {
            FastMemcpy(&buffer[pos], editor_buffer[i], len);
            pos += len;
        }
        if (i < total_lines - 1) {
            buffer[pos++] = '\n';
        }
    }

    int written = VfsWriteFile(filename, buffer, (uint32_t)total_len);
    KernelFree(buffer);

    if (written < 0 || written != total_len) {
        PrintKernel("\nError: Write failed\n");
        return 0;
    }

    dirty = 0;
    PrintKernel("\nFile saved successfully!\n");
    return 1;
}

static void EditorLoad(void) {
    char buffer[4096];
    int bytes_read = VfsReadFile(filename, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        // New or empty file
        FastMemset(editor_buffer, 0, sizeof(editor_buffer));
        total_lines = 1;
        current_line = 0;
        current_col = 0;
        dirty = 0;
        return;
    }

    if (bytes_read < (int)sizeof(buffer)) buffer[bytes_read] = '\0'; else buffer[sizeof(buffer)-1] = '\0';
    FastMemset(editor_buffer, 0, sizeof(editor_buffer));

    int line = 0, col = 0;
    for (int i = 0; i < bytes_read && line < MAX_LINES; i++) {
        char ch = buffer[i];

        if (ch == '\n') {
            line++;
            col = 0;
        } else if (ch == '\r') {
            continue;
        } else if (ch >= 32 && ch <= 126) {
            if (col < MAX_LINE_LEN - 1) {
                editor_buffer[line][col++] = ch;
            }
        }
    }

    total_lines = line + 1;
    if (total_lines <= 0) total_lines = 1;
    if (total_lines > MAX_LINES) total_lines = MAX_LINES;

    current_line = 0;
    current_col = 0;
    dirty = 0;
    clamp_cursor();
}

// Convert string to integer (simple atoi)
static int str_to_int(const char* str) {
    if (!str) return 0;
    int result = 0;
    int i = 0;
    while (str[i] >= '0' && str[i] <= '9' && i < 10) {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    return result;
}

// Handle goto line mode
static void handle_goto_mode(char c) {
    if (c == 27) { // ESC - cancel goto
        goto_mode = 0;
        goto_pos = 0;
        FastMemset(goto_buffer, 0, sizeof(goto_buffer));
        return;
    }

    if (c == '\n' || c == '\r') { // Enter - execute goto
        goto_buffer[goto_pos] = '\0';
        int target_line = str_to_int(goto_buffer);

        if (target_line > 0 && target_line <= total_lines) {
            current_line = target_line - 1; // Convert to 0-based
            current_col = 0;
            clamp_cursor();
        }

        goto_mode = 0;
        goto_pos = 0;
        FastMemset(goto_buffer, 0, sizeof(goto_buffer));
        return;
    }

    if (c == '\b' || c == 127) { // Backspace
        if (goto_pos > 0) {
            goto_pos--;
            goto_buffer[goto_pos] = '\0';
        }
        return;
    }

    if (c >= '0' && c <= '9' && goto_pos < 9) { // Digit
        goto_buffer[goto_pos] = c;
        goto_pos++;
        goto_buffer[goto_pos] = '\0';
    }
}

static void EditorInsertChar(const char c) {
    if (current_col >= MAX_LINE_LEN - 1) return;

    // Shift characters right
    for (int i = MAX_LINE_LEN - 2; i > current_col; i--) {
        editor_buffer[current_line][i] = editor_buffer[current_line][i - 1];
    }

    editor_buffer[current_line][current_col] = c;
    current_col++;
    // Ensure line is always null-terminated
    editor_buffer[current_line][MAX_LINE_LEN - 1] = '\0';
    dirty = 1;
}

static void EditorDeleteChar(void) {
    if (current_col > 0) {
        current_col--;
        // Shift characters left
        for (int i = current_col; i < MAX_LINE_LEN - 1; i++) {
            editor_buffer[current_line][i] = editor_buffer[current_line][i + 1];
        }
        // Ensure termination at end
        editor_buffer[current_line][MAX_LINE_LEN - 1] = '\0';
        dirty = 1;
    } else if (current_line > 0) {
        // Join with previous line
        int prev_len = get_line_length(current_line - 1);

        // Check if we can fit the current line
        int curr_len = get_line_length(current_line);
        if (prev_len + curr_len < MAX_LINE_LEN - 1) {
            // Copy current line to end of previous line
            int remaining_space = MAX_LINE_LEN - 1 - prev_len;
            if (remaining_space > 0 && curr_len <= remaining_space) {
                FastMemcpy(&editor_buffer[current_line - 1][prev_len],
                          editor_buffer[current_line], curr_len);
                editor_buffer[current_line - 1][prev_len + curr_len] = '\0';
            }
            // Move lines up
            for (int i = current_line; i < total_lines - 1 && i < MAX_LINES - 1; i++) {
                FastMemcpy(editor_buffer[i], editor_buffer[i + 1], MAX_LINE_LEN);
            }

            // Clear last line
            if (total_lines > 0 && total_lines <= MAX_LINES) {
                FastMemset(editor_buffer[total_lines - 1], 0, MAX_LINE_LEN);
            }

            current_line--;
            current_col = prev_len;
            total_lines--;
            if (total_lines <= 0) total_lines = 1;
            dirty = 1;
        }
    }
    clamp_cursor();
}

static void EditorNewLine(void) {
    if (total_lines >= MAX_LINES) return;

    // Move lines down
    for (int i = (total_lines < MAX_LINES ? total_lines : MAX_LINES - 1); i > current_line + 1; i--) {
        FastMemcpy(editor_buffer[i], editor_buffer[i-1], MAX_LINE_LEN);
    }

    // Split current line at cursor
    if (current_line + 1 < MAX_LINES) {
        FastMemset(editor_buffer[current_line + 1], 0, MAX_LINE_LEN);

        // Copy text after cursor to new line using FastMemcpy
        int curr_len = get_line_length(current_line);
        int copy_len = curr_len - current_col;
        if (copy_len > 0) {
            if (copy_len > MAX_LINE_LEN - 1) copy_len = MAX_LINE_LEN - 1;
            FastMemcpy(editor_buffer[current_line + 1],
                      &editor_buffer[current_line][current_col], copy_len);
            editor_buffer[current_line + 1][copy_len] = '\0';
        }

        // Truncate current line at cursor
        editor_buffer[current_line][current_col] = '\0';
    }

    current_line++;
    current_col = 0;
    if (total_lines < MAX_LINES) total_lines++;
    dirty = 1;
    clamp_cursor();
}

void EditorOpen(const char* file) {
    if (!file) return;

    // Safely copy filename using Fast string copy
    FastStrCopy(filename, file, FILENAME_MAX_LEN + 1);

    EditorLoad();

    while (1) {
        EditorRefresh();

        // Wait for input
        while (!HasInput()) {
            Yield();
        }

        char c = GetChar();

        // Handle goto line mode first
        if (goto_mode) {
            handle_goto_mode(c);
            continue;
        }

        // Handle control characters first
        if (c == 19) { // Ctrl+S - Save
            EditorSave();
            continue;
        }

        if (c == 17) { // Ctrl+Q - Quit
            if (dirty) {
                PrintKernel("\nFile has unsaved changes! Press Ctrl+Q again to quit without saving, or Ctrl+S to save.\n");
                // Wait for another key
                while (!HasInput()) Yield();
                char confirm = GetChar();
                if (confirm != 17) { // Not Ctrl+Q again
                    continue;
                }
            }
            PrintKernel("\nExiting editor...\n");
            break;
        }

        if (c == 12) { // Ctrl+L - Goto line
            goto_mode = 1;
            goto_pos = 0;
            FastMemset(goto_buffer, 0, sizeof(goto_buffer));
            continue;
        }

        // Navigation
        if (c == 23) { // Ctrl+W - Up
            if (current_line > 0) {
                current_line--;
            }
        } else if (c == 24) { // Ctrl+X - Down
            if (current_line < total_lines - 1) {
                current_line++;
            }
        } else if (c == 1) { // Ctrl+A - Left
            if (current_col > 0) {
                current_col--;
            } else if (current_line > 0) {
                current_line--;
                current_col = get_line_length(current_line);
            }
        } else if (c == 4) { // Ctrl+D - Right
            int line_len = get_line_length(current_line);
            if (current_col < line_len) {
                current_col++;
            } else if (current_line < total_lines - 1) {
                current_line++;
                current_col = 0;
            }
        }
        // Text editing
        else if (c == '\n' || c == '\r') {
            EditorNewLine();
        } else if (c == '\b' || c == 127) { // Backspace or DEL
            EditorDeleteChar();
        } else if (c >= 32 && c <= 126) {
            // Printable character
            EditorInsertChar(c);
        }

        // Always clamp cursor after any operation
        clamp_cursor();
    }
}