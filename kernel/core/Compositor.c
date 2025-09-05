#include "Compositor.h"
#include "Console.h"
#include "Font.h"
#include "KernelHeap.h"
#include "MLFQ.h"
#include "MemOps.h"
#include "StringOps.h"
#include "Vesa.h"
#include "Pallete.h"
#include "Spinlock.h"

// --- Globals ---
#define MAX_WINDOWS 16
static Window* g_window_list_head = NULL;
static Window* g_window_list_tail = NULL;
static vbe_info_t* g_vbe_info = NULL;
static uint32_t* g_compositor_buffer = NULL;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static Window* g_focused_window = NULL;

typedef struct {
    Window*             window;
    WindowTextState     state;
    bool                in_use;
} WindowStateMapping;

static WindowStateMapping g_window_state_map[MAX_WINDOWS];
static irq_flags_t g_text_lock = 0;
static Window* g_vfshell_window = NULL;
// Get window by title
Window* GetWindowByTitle(const char* title) {
    if (!title) return NULL;

    irq_flags_t flags = SpinLockIrqSave(&g_text_lock);

    Window* current = g_window_list_head;
    while (current) {
        if (current->title && FastStrCmp(current->title, title) == 0) {
            SpinUnlockIrqRestore(&g_text_lock, flags);
            return current;
        }
        current = current->next;
    }

    SpinUnlockIrqRestore(&g_text_lock, flags);
    return NULL;
}

WindowTextState* GetWindowTextState(Window* window) {
    if (!window) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_window_state_map[i].in_use &&
            g_window_state_map[i].window == window) {
            return &g_window_state_map[i].state;
            }
    }
    // Allocate new state
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!g_window_state_map[i].in_use) {
            g_window_state_map[i].window       = window;
            g_window_state_map[i].in_use       = true;
            FastMemset(&g_window_state_map[i].state, 0, sizeof(WindowTextState));
            g_window_state_map[i].state.needs_refresh = true;
            return &g_window_state_map[i].state;
        }
    }
    return NULL;
}

// Initialize window for text mode
void WindowInitTextMode(Window* window) {
    if (!window) return;

    WindowTextState* state = GetWindowTextState(window);
    if (!state) return;

    // Clear text buffer
    FastMemset(state->buffer, 0, sizeof(state->buffer));
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->scroll_offset = 0;
    state->needs_refresh = true;

    // Clear window background
    WindowFill(window, WINDOW_BG);

    // Draw title bar
    WindowDrawRect(window, 0, 0, window->rect.width, 20, TITLE_BAR);
    if (window->title) {
        WindowDrawString(window, 5, 2, window->title, TERMINAL_TEXT);
    }
}

// Scroll window text up by one line
void WindowScrollUp(Window* window) {
    WindowTextState* state = GetWindowTextState(window);
    if (!state) return;

    // Move all lines up
    for (int row = 0; row < WINDOW_TEXT_ROWS - 1; row++) {
        for (int col = 0; col < WINDOW_TEXT_COLS; col++) {
            state->buffer[row][col] = state->buffer[row + 1][col];
        }
    }

    // Clear bottom line
    FastMemset(state->buffer[WINDOW_TEXT_ROWS - 1], 0, WINDOW_TEXT_COLS);

    state->needs_refresh = true;
}

// Print a character to window
void WindowPrintChar(Window* window, char c) {
    if (!window) return;

    irq_flags_t flags = SpinLockIrqSave(&g_text_lock);

    WindowTextState* state = GetWindowTextState(window);
    if (!state) {
        SpinUnlockIrqRestore(&g_text_lock, flags);
        return;
    }

    switch (c) {
        case '\n':
            state->cursor_row++;
            state->cursor_col = 0;
            break;

        case '\r':
            state->cursor_col = 0;
            break;

        case '\t':
            state->cursor_col = (state->cursor_col + 4) & ~3; // Align to 4
            if (state->cursor_col >= WINDOW_TEXT_COLS) {
                state->cursor_col = 0;
                state->cursor_row++;
            }
            break;

        case '\b':
            if (state->cursor_col > 0) {
                state->cursor_col--;
                state->buffer[state->cursor_row][state->cursor_col] = ' ';
            }
            break;

        default:
            if (c >= 32 && c < 127) { // Printable ASCII
                if (state->cursor_col < WINDOW_TEXT_COLS && state->cursor_row < WINDOW_TEXT_ROWS) {
                    state->buffer[state->cursor_row][state->cursor_col] = c;
                    state->cursor_col++;

                    if (state->cursor_col >= WINDOW_TEXT_COLS) {
                        state->cursor_col = 0;
                        state->cursor_row++;
                    }
                }
            }
            break;
    }

    // Handle scrolling
    if (state->cursor_row >= WINDOW_TEXT_ROWS) {
        WindowScrollUp(window);
        state->cursor_row = WINDOW_TEXT_ROWS - 1;
    }

    state->needs_refresh = true;
    window->needs_redraw = true;

    SpinUnlockIrqRestore(&g_text_lock, flags);
}

// Print string to window
void WindowPrintString(Window* window, const char* str) {
    if (!window || !str) return;

    while (*str) {
        WindowPrintChar(window, *str);
        str++;
    }
}

// Clear window text
void WindowClearText(Window* window) {
    WindowTextState* state = GetWindowTextState(window);
    if (!state) return;

    irq_flags_t flags = SpinLockIrqSave(&g_text_lock);

    FastMemset(state->buffer, 0, sizeof(state->buffer));
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->needs_refresh = true;
    window->needs_redraw = true;

    SpinUnlockIrqRestore(&g_text_lock, flags);
}

// Update VFCompositor to cache VFShell window reference
void VFCompositor(void) {
    Snooze();

    if (!VBEIsInitialized()) {
        PrintKernel("VFCompositor: VBE not initialized, waiting...\n");
        while (!VBEIsInitialized()) {
            MLFQYield();
        }
    }

    WindowManagerInit();

    // Create VFShell window and cache reference
    g_vfshell_window = CreateWindow(50, 50, 640, 480, "VFShell");
    if (g_vfshell_window) {
        WindowInitTextMode(g_vfshell_window);
        WindowPrintString(g_vfshell_window, "VFShell - Kernel Log\n");
        WindowPrintString(g_vfshell_window, "===================\n\n");
    }

    while (1) {
        if (VBEIsInitialized()) {
            // Refresh text content if needed
            if (g_vfshell_window && g_vfshell_window->needs_redraw) {
                WindowTextState* state = GetWindowTextState(g_vfshell_window);
                if (state && state->needs_refresh) {
                    // Redraw text content
                    int text_y = 25; // Start below title bar
                    for (int row = 0; row < WINDOW_TEXT_ROWS && text_y < g_vfshell_window->rect.height - FONT_HEIGHT; row++) {
                        int text_x = 5;
                        for (int col = 0; col < WINDOW_TEXT_COLS && state->buffer[row][col] != 0; col++) {
                            char single_char[2] = {state->buffer[row][col], 0};
                            WindowDrawString(g_vfshell_window, text_x, text_y, single_char, TERMINAL_TEXT);
                            text_x += FONT_WIDTH;
                        }
                        text_y += FONT_HEIGHT;
                    }
                    state->needs_refresh = false;
                }
            }

            WindowManagerRun();
            MLFQYield();
        } else {
            MLFQYield();
        }
    }

    Unsnooze();
}

// Get VFShell window (cached reference)
Window* GetVFShellWindow(void) {
    return g_vfshell_window;
}

static void DrawMouseCursor() {
    if (!g_vbe_info) return;
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            if (g_mouse_y + y >= 0 && g_mouse_x + x >= 0 &&
                g_mouse_y + y < (int)g_vbe_info->height &&
                g_mouse_x + x < (int)g_vbe_info->width) {
                g_compositor_buffer[(g_mouse_y + y) * g_vbe_info->width + (g_mouse_x + x)] = TERMINAL_TEXT;
            }
        }
    }
}

static void CompositeAndDraw() {
    if (!g_vbe_info) return;

    uint32_t background_color = TERMINAL_BG;
    for (int i = 0; i < g_vbe_info->width * g_vbe_info->height; i++) {
        g_compositor_buffer[i] = background_color;
    }

    for (Window* win = g_window_list_head; win != NULL; win = win->next) {
        if (win->needs_redraw) {
            WindowFill(win, WINDOW_BG);
            WindowDrawRect(win, 0, 0, win->rect.width, 20, TITLE_BAR);
            WindowDrawString(win, 5, 4, win->title, TERMINAL_TEXT);
            win->needs_redraw = false;
        }
        // Clip source and destination ranges when window is partially off-screen
        int y0 = MAX(0, -win->rect.y);
        int y1 = MIN(win->rect.height, (int)g_vbe_info->height - win->rect.y);
        int x0 = MAX(0, -win->rect.x);
        int x1 = MIN(win->rect.width, (int)g_vbe_info->width - win->rect.x);
        for (int y = y0; y < y1; y++) {
            uint32_t* src = &win->back_buffer[y * win->rect.width + x0];
            uint32_t* dst = &g_compositor_buffer[(win->rect.y + y) * g_vbe_info->width + (win->rect.x + x0)];
            FastMemcpy(dst, src, (x1 - x0) * 4);
        }
    }

    DrawMouseCursor();
    const uint32_t bpp   = g_vbe_info->bpp;
    const uint32_t pitch = g_vbe_info->pitch;
    if (bpp != 32 || pitch == 0) {
        // Fallback: avoid undefined behavior on unsupported modes
        return;
    }
    uint8_t* dst        = (uint8_t*)g_vbe_info->framebuffer;
    uint8_t* src        = (uint8_t*)g_compositor_buffer;
    const uint32_t row_bytes = g_vbe_info->width * 4;
    for (uint32_t row = 0; row < g_vbe_info->height; row++) {
        FastMemcpy(dst + row * pitch, src + row * row_bytes, row_bytes);
    }
}

// --- Public API ---

void WindowManagerInit(void) {
    g_vbe_info = VBEGetInfo();
    if (!g_vbe_info) {
        PrintKernelError("Window Manager: VBE not initialized!\n");
        return;
    }
    uint32_t buffer_size = g_vbe_info->width * g_vbe_info->height * 4;
    g_compositor_buffer = (uint32_t*)KernelMemoryAlloc(buffer_size);
    if (!g_compositor_buffer) {
        PrintKernelError("Window Manager: Failed to allocate compositor buffer!\n");
        return;
    }
    // Snooze();
    PrintKernelSuccess("Window Manager Initialized\n");
}

void WindowManagerRun(void) {
    CompositeAndDraw();
}

Window* CreateWindow(int x, int y, int width, int height, const char* title) {
    if (!g_vbe_info) return NULL;
    Window* win = (Window*)KernelMemoryAlloc(sizeof(Window));
    if (!win) return NULL;
    uint32_t back_buffer_size = width * height * 4;
    win->back_buffer = (uint32_t*)KernelMemoryAlloc(back_buffer_size);
    if (!win->back_buffer) {
        KernelFree(win);
        return NULL;
    }
    win->rect.x = x;
    win->rect.y = y;
    win->rect.width = width;
    win->rect.height = height;
    size_t title_len = StringLength(title);
    char* title_copy = (char*)KernelMemoryAlloc(title_len + 1);
    if (!title_copy) {
        // Roll back previously allocated resources on failure
        KernelFree(win->back_buffer);
        KernelFree(win);
        return NULL;
    }
    strcpy(title_copy, title);
    win->title = title_copy;
    win->needs_redraw = true;
    win->is_moving = false;
    win->next = NULL;
    win->prev = g_window_list_tail;
    if (g_window_list_tail) {
        g_window_list_tail->next = win;
    } else {
        g_window_list_head = win;
    }
    g_window_list_tail = win;
    return win;
}


void DestroyWindow(Window* window) {
    // Clean up text-state mapping for this window
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_window_state_map[i].in_use &&
            g_window_state_map[i].window == window) {
            g_window_state_map[i].in_use   = false;
            g_window_state_map[i].window   = NULL;
            break;
        }
    }
    // Unlink from neighboring windows
    if (window->prev) window->prev->next = window->next;
    if (window->next) window->next->prev = window->prev;
    // Update head/tail if needed
    if (g_window_list_head == window) g_window_list_head = window->next;
    if (g_window_list_tail == window) g_window_list_tail = window->prev;
    // Clear pointers to avoid accidental reuse
    window->next = window->prev = NULL;
    // Free the copied title string
    KernelFree((void*)window->title);
    KernelFree(window->back_buffer);
    KernelFree(window);
}

void WindowFill(Window* window, uint32_t color) {
    if (!window) return;
    for (int i = 0; i < window->rect.width * window->rect.height; i++) {
        window->back_buffer[i] = color;
    }
}

void WindowDrawRect(Window* window, int x, int y, int width, int height, uint32_t color) {
    if (!window) return;
    for (int row = y; row < y + height; row++) {
        if (row < 0 || row >= window->rect.height) continue;
        for (int col = x; col < x + width; col++) {
            if (col < 0 || col >= window->rect.width) continue;
            window->back_buffer[row * window->rect.width + col] = color;
        }
    }
}

void WindowDrawString(Window* window, int x, int y, const char* str, uint32_t fg_color) {
    if (!window || !str) return;
    int current_x = x;
    int current_y = y;
    for (int i = 0; str[i] != '\0'; i++) {
        const unsigned char* glyph = console_font[(unsigned char)str[i]];
        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int col = 0; col < FONT_WIDTH; col++) {
                if ((glyph[row] >> (7 - col)) & 1) {
                    int pixel_x = current_x + col;
                    int pixel_y = current_y + row;
                    if (pixel_x >= 0 && pixel_x < window->rect.width && pixel_y >= 0 && pixel_y < window->rect.height) {
                         window->back_buffer[pixel_y * window->rect.width + pixel_x] = fg_color;
                    }
                }
            }
        }
        current_x += FONT_WIDTH;
    }
}

// --- Input Event Handlers ---

void OnKeyPress(char c) {
    if (g_focused_window) {
        WindowPrintChar(g_focused_window, c);
    }
}

void OnMouseMove(int x, int y, int dx, int dy) {
    if (!g_vbe_info) return;
    g_mouse_x = x;
    g_mouse_y = y;
    if (g_focused_window && g_focused_window->is_moving) {
        int new_x = g_focused_window->rect.x + dx;
        int new_y = g_focused_window->rect.y + dy;

        // Keep at least part of the window visible
        int min_visible = 20; // At least title bar should be visible
        if (new_x > (int)g_vbe_info->width - min_visible)
            new_x = g_vbe_info->width - min_visible;
        if (new_x < -(g_focused_window->rect.width - min_visible))
            new_x = -(g_focused_window->rect.width - min_visible);
        if (new_y > (int)g_vbe_info->height - min_visible)
            new_y = g_vbe_info->height - min_visible;
        if (new_y < 0)
            new_y = 0;

        g_focused_window->rect.x = new_x;
        g_focused_window->rect.y = new_y;
    }
}

void OnMouseButtonDown(int x, int y, uint8_t button) {
    if (button == 1) { // Left button
        Window* top_window = NULL;
        // Iterate backwards to find the topmost visible window
        for (Window* win = g_window_list_tail; win != NULL; win = win->prev) {
            if (x >= win->rect.x && x < win->rect.x + win->rect.width &&
                y >= win->rect.y && y < win->rect.y + win->rect.height) {
                top_window = win;
                break; // First match is the topmost window
                }
        }
        if (top_window) {
            g_focused_window = top_window;
            // Move focused window to front (tail of list for correct rendering order)
            if (top_window != g_window_list_tail) {
                // Remove from current position
                if (top_window->prev)
                    top_window->prev->next = top_window->next;
                if (top_window->next)
                    top_window->next->prev = top_window->prev;
                if (g_window_list_head == top_window)
                    g_window_list_head = top_window->next;
                // Add to tail
                top_window->prev = g_window_list_tail;
                top_window->next = NULL;
                if (g_window_list_tail)
                    g_window_list_tail->next = top_window;
                g_window_list_tail = top_window;
            }
            // Check if the click is on the title bar
            if (y - top_window->rect.y < 20) {
                g_focused_window->is_moving = true;
            }
        }
    }
}

void OnMouseButtonUp(int x, int y, uint8_t button) {
    if (button == 1 && g_focused_window) { // Left button
        g_focused_window->is_moving = false;
    }
}