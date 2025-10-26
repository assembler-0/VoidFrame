#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "Window.h"
#include "stdbool.h"
#include "stdint.h"
#include "SpinlockRust.h"
#include "Vesa.h"

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

#define MAX_WINDOWS 16
#define MAX_TITLE_LENGTH 64
#define TASKBAR_HEIGHT 28
#define START_BTN_WIDTH 80

typedef struct {
    int x, y, w, h;
    Window* win;
} TaskButton;

#define MAX_TASK_BUTTONS MAX_WINDOWS

typedef struct {
    Window*             window;
    WindowTextState     state;
    bool                in_use;
} WindowStateMapping;

typedef struct {
    Window* g_window_list_head;
    Window* g_window_list_tail;
    vbe_info_t* g_vbe_info;
    uint32_t* g_compositor_buffer;
    int g_mouse_x;
    int g_mouse_y;
    Window* g_focused_window;
    TaskButton g_task_buttons[MAX_TASK_BUTTONS];
    int g_task_button_count;
    Window* g_start_menu_window;
    Window* g_pending_destroy[MAX_WINDOWS];
    int g_pending_destroy_count;
    WindowStateMapping g_window_state_map[MAX_WINDOWS];
    RustSpinLock* g_text_lock;
} CompositorContext;

// New function declarations
void VFCompositorRequestInit(const char * str);
void VFCompositor(void);
void VFCompositorShutdown(CompositorContext* ctx);

extern CompositorContext g_compositor_ctx;

// Window management functions
void CompositorInit(CompositorContext* ctx);
Window* GetWindowByTitle(CompositorContext* ctx, const char* title);
Window* CreateWindow(CompositorContext* ctx, int x, int y, int width, int height, const char* title);
void DestroyWindow(CompositorContext* ctx, Window* window);
void RequestDestroyWindow(CompositorContext* ctx, Window* w);

// Window drawing and text functions
void WindowFill(Window* window, uint32_t color);
void WindowDrawRect(Window* window, int x, int y, int width, int height, uint32_t color);
void WindowDrawString(Window* window, int x, int y, const char* str, uint32_t fg_color);
void WindowDrawChar(Window* window, int x, int y, char c, uint32_t fg_color);
WindowTextState* GetWindowTextState(CompositorContext* ctx, Window* window);
void WindowInitTextMode(CompositorContext* ctx, Window* window);
void WindowPrintChar(CompositorContext* ctx, Window* window, char c);
void WindowPrintString(CompositorContext* ctx, Window* window, const char* str);
void WindowScrollUp(CompositorContext* ctx, Window* window);
void WindowClearText(CompositorContext* ctx, Window* window);

// Input event handlers
void OnMouseMove(CompositorContext* ctx, int x, int y, int dx, int dy);
void OnMouseButtonDown(CompositorContext* ctx, int x, int y, uint8_t button);
void OnMouseButtonUp(CompositorContext* ctx, int x, int y, uint8_t button);

#endif // WINDOW_MANAGER_H
