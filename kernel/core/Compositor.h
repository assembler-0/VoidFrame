#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "Window.h"
#include "stdbool.h"
#include "stdint.h"

// Window text management
#define WINDOW_TEXT_ROWS 30
#define WINDOW_TEXT_COLS 80
#define FONT_WIDTH 8
#define FONT_HEIGHT 16

typedef struct {
    char buffer[WINDOW_TEXT_ROWS][WINDOW_TEXT_COLS];
    int cursor_row;
    int cursor_col;
    int scroll_offset;
    bool needs_refresh;
} WindowTextState;

// New function declarations
Window* GetWindowByTitle(const char* title);
void WindowInitTextMode(Window* window);
void WindowPrintChar(Window* window, char c);
void WindowPrintString(Window* window, const char* str);
void WindowScrollUp(Window* window);
void WindowClearText(Window* window);
WindowTextState* GetWindowTextState(Window* window);
void WindowManagerInit(void);
void WindowManagerRun(void);
Window* CreateWindow(int x, int y, int width, int height, const char* title);
void DestroyWindow(Window* window);
void WindowFill(Window* window, uint32_t color);
void WindowDrawRect(Window* window, int x, int y, int width, int height, uint32_t color);
void WindowDrawString(Window* window, int x, int y, const char* str, uint32_t fg_color);
void VFCompositor(void);
Window* GetVFShellWindow(void);
#endif // WINDOW_MANAGER_H
