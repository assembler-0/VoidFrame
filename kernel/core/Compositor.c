#include "Compositor.h"
#include "Console.h"
#include "Font.h"
#include "KernelHeap.h"
#include "MLFQ.h"
#include "MemOps.h"
#include "StringOps.h"
#include "Vesa.h"

// In Compositor.c:
#define TERMINAL_BG     0x1E1E1E  // Dark gray
#define TERMINAL_TEXT   0x00FF00  // Classic green
#define WINDOW_BG       0x2F3349  // Modern blue-gray
#define TITLE_BAR       0x4C566A  // Medium gray

// --- Globals ---

static Window* g_window_list_head = NULL;
static Window* g_window_list_tail = NULL;
static vbe_info_t* g_vbe_info = NULL;
static uint32_t* g_compositor_buffer = NULL;
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static Window* g_focused_window = NULL;

void VFCompositor(void) {
    Snooze();
    if (VBEIsInitialized()) {
        WindowManagerInit();
        CreateWindow(50, 50, 400, 250, "Window 1");
        CreateWindow(150, 150, 500, 350, "Window 2");
    }
    while (1) {
        if (VBEIsInitialized()) {
            WindowManagerRun();
            MLFQYield();
        } else {
            MLFQYield();
        }
    }
    Unsnooze();
}

// --- Private Functions ---

static void DrawMouseCursor() {
    if (!g_vbe_info) return;
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 10; x++) {
            if (g_mouse_y + y < g_vbe_info->height && g_mouse_x + x < g_vbe_info->width) {
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

        for (int y = 0; y < win->rect.height; y++) {
            if (win->rect.y + y >= g_vbe_info->height) continue;
            for (int x = 0; x < win->rect.width; x++) {
                if (win->rect.x + x >= g_vbe_info->width) continue;
                uint32_t pixel = win->back_buffer[y * win->rect.width + x];
                g_compositor_buffer[(win->rect.y + y) * g_vbe_info->width + (win->rect.x + x)] = pixel;
            }
        }
    }

    DrawMouseCursor();
    FastMemcpy((void*)g_vbe_info->framebuffer, g_compositor_buffer, g_vbe_info->width * g_vbe_info->height * 4);
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
    if (g_window_list_head == window) g_window_list_head = window->next;
    if (g_window_list_tail == window) g_window_list_tail = window->prev;
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
        SerialWriteF("Key '%c' for window \"%s\"\n", c, g_focused_window->title);
    }
}

void OnMouseMove(int x, int y, int dx, int dy) {
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