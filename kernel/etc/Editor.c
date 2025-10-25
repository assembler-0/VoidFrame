#include "Editor.h"
#include "Console.h"
#include "KernelHeap.h"
#include "MemOps.h"
#include "PS2.h"
#include "input/Keyboard.h"
#include "Scheduler.h"
#include "StringOps.h"
#include "VFS.h"

#define MAX_BUFFER_SIZE 4096

static char* buffer = NULL;
static int buffer_size = 0;
static int cursor_pos = 0;
static char current_filename[128];
static int dirty = 0;

static int EditorFindLineStart(int target_line) {
    if (target_line <= 1) return 0;
    int line = 1;
    for (int i = 0; i < buffer_size; i++) {
        if (buffer[i] == '\n') {
            line++;
            if (line == target_line) {
                // Start is next character or end of buffer
                return (i + 1 <= buffer_size) ? (i + 1) : buffer_size;
            }
        }
    }
    // If target_line is beyond existing lines, return end
    return buffer_size;
}

static void EditorGetCursorLineCol(int* out_line, int* out_col) {
    int line = 1, col = 0;
    for (int i = 0; i < buffer_size && i < cursor_pos; i++) {
        if (buffer[i] == '\n') { line++; col = 0; }
        else { col++; }
    }
    if (out_line) *out_line = line;
    if (out_col) *out_col = col;
}

static void EditorGotoLinePrompt(void) {
    // Simple inline prompt at bottom
    PrintKernel("\nGo to line: ");
    char input[12];
    int len = 0;
    while (1) {
        while (!HasInput()) {
            Yield();
        }
        char c = GetChar();
        if (c == '\n' || c == '\r') {
            break;
        } else if ((c == '\b' || c == 127)) {
            if (len > 0) {
                len--;
                PrintKernel("\b \b");
            }
        } else if (c >= '0' && c <= '9') {
            if (len < (int)sizeof(input) - 1) {
                input[len++] = c;
                char s[2] = { c, 0 };
                PrintKernel(s);
            }
        }
    }
    input[len] = 0;
    if (len == 0) return;
    int target = 0;
    for (int i = 0; i < len; i++) { target = target * 10 + (input[i] - '0'); }
    if (target < 1) target = 1;
    int pos = EditorFindLineStart(target);
    if (pos < 0) pos = 0;
    if (pos > buffer_size) pos = buffer_size;
    cursor_pos = pos;
}

static void EditorRefresh(void) {
    ClearScreen();
    PrintKernel("VoidFrame Editor - ");
    PrintKernel(current_filename);
    if (dirty) PrintKernel(" *");
    PrintKernel(" (Ctrl+S=Save, Ctrl+Q=Quit, Ctrl+A: Cursor left, Ctrl+D: Cursor right)\n");
    PrintKernel("----------------------------------------\n");
    
    // Print buffer content with line numbers and cursor
    int current_line = 1;
    int printed_any = 0;
    // Print first line number
    PrintKernelInt(current_line);
    PrintKernel(" | ");
    for (int i = 0; i < buffer_size; i++) {
        if (i == cursor_pos) { PrintKernel("|"); }
        char c[2] = { buffer[i], 0 };
        PrintKernel(c);
        printed_any = 1;
        if (buffer[i] == '\n' && i + 1 < buffer_size) {
            current_line++;
            PrintKernelInt(current_line);
            PrintKernel(" | ");
        }
    }
    if (!printed_any) {
        // Empty buffer: just show cursor pipe after line number
        if (cursor_pos == 0) PrintKernel("_");
    } else if (cursor_pos >= buffer_size) {
        PrintKernel("_");
    }
    
    PrintKernel("\n----------------------------------------\n");
    PrintKernel("Size: ");
    PrintKernelInt(buffer_size);
    int line, col;
    EditorGetCursorLineCol(&line, &col);
    PrintKernel("  Cursor: ");
    PrintKernelInt(cursor_pos);
    PrintKernel("  Line: ");
    PrintKernelInt(line);
    PrintKernel("  Col: ");
    PrintKernelInt(col);
    PrintKernel("\n");
}

static void EditorSave(void) {
    if (!buffer || buffer_size == 0) {
        VfsWriteFile(current_filename, "", 0);
    } else {
        VfsWriteFile(current_filename, buffer, buffer_size);
    }
    dirty = 0;
    PrintKernel("\nSaved!\n");
}

static void EditorLoad(void) {
    char* temp_buffer = (char*)KernelMemoryAlloc(MAX_BUFFER_SIZE);
    if (!temp_buffer) {
        buffer_size = 0;
        cursor_pos = 0;
        return;
    }

    int bytes = VfsReadFile(current_filename, temp_buffer, MAX_BUFFER_SIZE - 1);
    
    if (bytes <= 0) {
        KernelFree(temp_buffer);
        buffer_size = 0;
        cursor_pos = 0;
        return;
    }
    
    if (buffer) KernelFree(buffer);
    buffer = KernelMemoryAlloc(bytes);
    if (!buffer) {
        KernelFree(temp_buffer);
        return;
    }
    
    FastMemcpy(buffer, temp_buffer, bytes);
    KernelFree(temp_buffer);
    buffer_size = bytes;
    cursor_pos = 0;
    dirty = 0;
}

static void EditorInsertChar(char c) {
    char* new_buffer = KernelMemoryAlloc(buffer_size + 1);
    if (!new_buffer) return;
    
    // Copy before cursor
    if (cursor_pos > 0) {
        FastMemcpy(new_buffer, buffer, cursor_pos);
    }
    
    // Insert character
    new_buffer[cursor_pos] = c;
    
    // Copy after cursor
    if (cursor_pos < buffer_size) {
        FastMemcpy(new_buffer + cursor_pos + 1, buffer + cursor_pos, buffer_size - cursor_pos);
    }
    
    if (buffer) KernelFree(buffer);
    buffer = new_buffer;
    buffer_size++;
    cursor_pos++;
    dirty = 1;
}

static void EditorDeleteChar(void) {
    if (cursor_pos == 0 || buffer_size == 0) return;
    
    char* new_buffer = KernelMemoryAlloc(buffer_size - 1);
    if (!new_buffer && buffer_size > 1) return;
    
    cursor_pos--;
    
    if (buffer_size == 1) {
        KernelFree(buffer);
        buffer = NULL;
        buffer_size = 0;
    } else {
        // Copy before cursor
        if (cursor_pos > 0) {
            FastMemcpy(new_buffer, buffer, cursor_pos);
        }
        
        // Copy after deleted char
        if (cursor_pos < buffer_size - 1) {
            FastMemcpy(new_buffer + cursor_pos, buffer + cursor_pos + 1, buffer_size - cursor_pos - 1);
        }
        
        KernelFree(buffer);
        buffer = new_buffer;
        buffer_size--;
    }
    
    dirty = 1;
}

void EditorOpen(const char* filename) {
    if (!filename) return;
    
    FastStrCopy(current_filename, filename, 127);
    
    if (buffer) {
        KernelFree(buffer);
        buffer = NULL;
    }
    buffer_size = 0;
    cursor_pos = 0;
    dirty = 0;
    
    EditorLoad();
    
    while (1) {
        EditorRefresh();
        
        while (!HasInput()) {
            Yield();
        }
        
        char c = GetChar();
        
        if (c == PS2_CalcCombo(K_CTRL, 'S')) { // Ctrl+S
            EditorSave();
            continue;
        }
        
        if (c == PS2_CalcCombo(K_CTRL, 'Q')) { // Ctrl+Q
            if (dirty) {
                PrintKernel("\nUnsaved changes! Press Ctrl+Q again to quit.\n");
                while (!HasInput()) Yield();
                if (GetChar() != 17) continue;
            }
            break;
        }
        
        if (c == PS2_CalcCombo(K_CTRL, 'A')) { // Ctrl+A - cursor left
            if (cursor_pos > 0) cursor_pos--;
        } else if (c == PS2_CalcCombo(K_CTRL, 'D')) { // Ctrl+D - cursor right
            if (cursor_pos < buffer_size) cursor_pos++;
        } else if (c == '\b' || c == 127) { // Backspace
            EditorDeleteChar();
        } else if (c >= 32 && c <= 126) { // Printable
            EditorInsertChar(c);
        } else if (c == '\n' || c == '\r') { // Enter
            EditorInsertChar('\n');
        } else if (c == PS2_CalcCombo(K_CTRL, 'L')) { // Ctrl+L: Go to line
            EditorGotoLinePrompt();
        }
    }
    
    if (buffer) {
        KernelFree(buffer);
        buffer = NULL;
    }
}