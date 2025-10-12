#include "Compositor.h"
#include "Console.h"
#include "Font.h"
#include "KernelHeap.h"
#include "MLFQ.h"
#include "MemOps.h"
#include "Pallete.h"
#include "Panic.h"
#include "Scheduler.h"
#include "SpinlockRust.h"
#include "StringOps.h"
#include "Vesa.h"

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// --- Globals ---
#define MAX_WINDOWS 16
#define MAX_TITLE_LENGTH 64
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
static RustSpinLock* g_text_lock = NULL;
static Window* g_vfshell_window = NULL;

void VFCompositorRequestInit(const char * str) {
    (void)str;
#ifndef VF_CONFIG_ENABLE_VFCOMPOSITOR
    PrintKernelError("System: VFCompositor disabled in this build\n");
    return;
#endif
    Snooze();
    static uint32_t cached_vfc_pid = 0;
    if (cached_vfc_pid) {
        CurrentProcessControlBlock* p = GetCurrentProcessByPID(cached_vfc_pid);
        if (p && p->state != PROC_TERMINATED) {
            PrintKernelWarning("System: VFCompositor already running\n");
            return;
        }
        cached_vfc_pid = 0;
    }
    PrintKernel("System: Creating VFCompositor...\n");
    uint32_t vfc_pid = CreateProcess("VFCompositor", VFCompositor);
    if (!vfc_pid) {
#ifndef VF_CONFIG_PANIC_OVERRIDE
        PANIC("CRITICAL: Failed to create VFCompositor process");
#else
        PrintKernelError("CRITICAL: Failed to create VFCompositor process\n");
#endif
    }
    cached_vfc_pid = vfc_pid;
    PrintKernelSuccess("System: VFCompositor created with PID: ");
    PrintKernelInt(vfc_pid);
    PrintKernel("\n");
}

// Get window by title
Window* GetWindowByTitle(const char* title) {
    if (!title) return NULL;

    uint64_t flags = rust_spinlock_lock_irqsave(g_text_lock);

    Window* current = g_window_list_head;
    while (current) {
        if (current->title && FastStrCmp(current->title, title) == 0) {
            rust_spinlock_unlock_irqrestore(g_text_lock, flags);
            return current;
        }
        current = current->next;
    }

    rust_spinlock_unlock_irqrestore(g_text_lock, flags);
    return NULL;
}

static void DrawMouseCursor() {
    if (!g_vbe_info || !g_compositor_buffer) return;

    for (int y = 0; y < 10 && (g_mouse_y + y) < g_vbe_info->height; y++) {
        for (int x = 0; x < 10 && (g_mouse_x + x) < g_vbe_info->width; x++) {
            int screen_x = g_mouse_x + x;
            int screen_y = g_mouse_y + y;
            if (screen_x >= 0 && screen_y >= 0) {
                g_compositor_buffer[screen_y * g_vbe_info->width + screen_x] = 0xFFFFFF; // White cursor
            }
        }
    }
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

    uint64_t flags = rust_spinlock_lock_irqsave(g_text_lock);

    WindowTextState* state = GetWindowTextState(window);
    if (!state) {
        rust_spinlock_unlock_irqrestore(g_text_lock, flags);
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

    rust_spinlock_unlock_irqrestore(g_text_lock, flags);
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

    uint64_t flags = rust_spinlock_lock_irqsave(g_text_lock);

    FastMemset(state->buffer, 0, sizeof(state->buffer));
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->needs_refresh = true;
    window->needs_redraw = true;

    rust_spinlock_unlock_irqrestore(g_text_lock, flags);
}

static void CompositeAndDraw() {
    if (!g_vbe_info) return;

    uint32_t background_color = TERMINAL_BG;
    for (int i = 0; i < g_vbe_info->width * g_vbe_info->height; i++) {
        g_compositor_buffer[i] = background_color;
    }

    for (Window* win = g_window_list_head; win != NULL; win = win->next) {
        if (!win->back_buffer) continue;
        
        // Safe clipping with proper bounds checking
        int src_y_start = MAX(0, -win->rect.y);
        int src_y_end = MIN(win->rect.height, (int)g_vbe_info->height - win->rect.y);
        int src_x_start = MAX(0, -win->rect.x);
        int src_x_end = MIN(win->rect.width, (int)g_vbe_info->width - win->rect.x);
        
        if (src_y_start >= src_y_end || src_x_start >= src_x_end) continue;
        
        for (int y = src_y_start; y < src_y_end; y++) {
            int screen_y = win->rect.y + y;
            if (screen_y < 0 || screen_y >= g_vbe_info->height) continue;
            
            int src_idx = y * win->rect.width + src_x_start;
            int dst_idx = screen_y * g_vbe_info->width + (win->rect.x + src_x_start);
            int copy_width = src_x_end - src_x_start;
            
            if (src_idx >= 0 && src_idx + copy_width <= win->rect.width * win->rect.height &&
                dst_idx >= 0 && dst_idx + copy_width <= g_vbe_info->width * g_vbe_info->height) {
                FastMemcpy(&g_compositor_buffer[dst_idx], &win->back_buffer[src_idx], copy_width * 4);
            }
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


// Update VFCompositor to cache VFShell window reference
void VFCompositor(void) {
    g_text_lock = rust_spinlock_new();
    if (!g_text_lock) {
        PrintKernelError("VFCompositor: Failed to initialize text lock\n");
        return;
    }
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
        g_focused_window = g_vfshell_window;
    }

    while (1) {
        if (VBEIsInitialized()) {
            // Render text content for all windows that need it
            Window* current = g_window_list_head;
            while (current) {
                WindowTextState* state = GetWindowTextState(current);
                if (state && state->needs_refresh) {
                    // Clear window and redraw title bar
                    WindowFill(current, WINDOW_BG);
                    WindowDrawRect(current, 0, 0, current->rect.width, 20, TITLE_BAR);
                    if (current->title) {
                        WindowDrawString(current, 5, 2, current->title, TERMINAL_TEXT);
                    }
                    
                    // Redraw text content
                    int text_y = 25; // Start below title bar
                    for (int row = 0; row < WINDOW_TEXT_ROWS && text_y < current->rect.height - FONT_HEIGHT; row++) {
                        int text_x = 5;
                        for (int col = 0; col < WINDOW_TEXT_COLS && state->buffer[row][col] != 0; col++) {
                            char single_char[2] = {state->buffer[row][col], 0};
                            WindowDrawString(current, text_x, text_y, single_char, TERMINAL_TEXT);
                            text_x += FONT_WIDTH;
                        }
                        text_y += FONT_HEIGHT;
                    }
                    state->needs_refresh = false;
                }
                current = current->next;
            }
            
            CompositeAndDraw();
        } else {
            Yield();
        }
    }

    Unsnooze();
}

// Get VFShell window (cached reference)
Window* GetVFShellWindow(void) {
    return g_vfshell_window;
}

// Window management functions
void WindowManagerInit(void) {
    g_vbe_info = VBEGetInfo();
    if (!g_vbe_info) {
        PrintKernelError("WindowManager: Failed to get VBE info\n");
        return;
    }
    
    size_t buffer_size = g_vbe_info->width * g_vbe_info->height * sizeof(uint32_t);
    g_compositor_buffer = (uint32_t*)KernelMemoryAlloc(buffer_size);
    if (!g_compositor_buffer) {
        PrintKernelError("WindowManager: Failed to allocate compositor buffer\n");
        return;
    }
    
    FastMemset(g_compositor_buffer, 0, buffer_size);
    FastMemset(g_window_state_map, 0, sizeof(g_window_state_map));
    
    g_mouse_x = g_vbe_info->width / 2;
    g_mouse_y = g_vbe_info->height / 2;
}

void WindowManagerRun(void) {
    if (!g_vbe_info || !g_compositor_buffer) return;
    
    // Clear compositor buffer
    FastMemset(g_compositor_buffer, 0, g_vbe_info->width * g_vbe_info->height * sizeof(uint32_t));
    
    // Render all windows
    Window* current = g_window_list_head;
    while (current) {
        if (current->needs_redraw && current->back_buffer) {
            // Copy window buffer to compositor buffer with bounds checking
            for (int y = 0; y < current->rect.height; y++) {
                int screen_y = current->rect.y + y;
                if (screen_y < 0 || screen_y >= g_vbe_info->height) continue;
                
                for (int x = 0; x < current->rect.width; x++) {
                    int screen_x = current->rect.x + x;
                    if (screen_x < 0 || screen_x >= g_vbe_info->width) continue;
                    
                    // Additional bounds check for buffer access
                    int compositor_idx = screen_y * g_vbe_info->width + screen_x;
                    int window_idx = y * current->rect.width + x;
                    
                    if (compositor_idx >= 0 && compositor_idx < (g_vbe_info->width * g_vbe_info->height) &&
                        window_idx >= 0 && window_idx < (current->rect.width * current->rect.height)) {
                        g_compositor_buffer[compositor_idx] = current->back_buffer[window_idx];
                    }
                }
            }
            current->needs_redraw = false;
        }
        current = current->next;
    }
    
    DrawMouseCursor();
    
    // Copy compositor buffer to screen
    FastMemcpy((void*)g_vbe_info->framebuffer, g_compositor_buffer, 
               g_vbe_info->width * g_vbe_info->height * sizeof(uint32_t));
}

Window* CreateWindow(int x, int y, int width, int height, const char* title) {
    Window* window = (Window*)KernelMemoryAlloc(sizeof(Window));
    if (!window) return NULL;
    
    window->rect.x = x;
    window->rect.y = y;
    window->rect.width = width;
    window->rect.height = height;
    window->needs_redraw = true;
    window->is_moving = false;
    window->move_offset_x = 0;
    window->move_offset_y = 0;
    window->next = NULL;
    window->prev = NULL;
    
    // Allocate back buffer
    size_t buffer_size = width * height * sizeof(uint32_t);
    window->back_buffer = (uint32_t*)KernelMemoryAlloc(buffer_size);
    if (!window->back_buffer) {
        KernelFree(window);
        return NULL;
    }
    FastMemset(window->back_buffer, 0, buffer_size);
    
    // Copy title
    if (title) {
        size_t title_len = FastStrlen(title, MAX_TITLE_LENGTH) + 1;
        char* title_copy = (char*)KernelMemoryAlloc(title_len);
        if (title_copy) {
            FastStrCopy(title_copy, title, title_len);
            window->title = title_copy;
        } else {
            window->title = NULL;
        }
    } else {
        window->title = NULL;
    }
    
    // Add to window list
    if (!g_window_list_head) {
        g_window_list_head = window;
        g_window_list_tail = window;
    } else {
        g_window_list_tail->next = window;
        window->prev = g_window_list_tail;
        g_window_list_tail = window;
    }
    
    return window;
}

void DestroyWindow(Window* window) {
    if (!window) return;
    
    // Remove from window list
    if (window->prev) {
        window->prev->next = window->next;
    } else {
        g_window_list_head = window->next;
    }
    
    if (window->next) {
        window->next->prev = window->prev;
    } else {
        g_window_list_tail = window->prev;
    }
    
    // Remove from state map
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (g_window_state_map[i].in_use && g_window_state_map[i].window == window) {
            g_window_state_map[i].in_use = false;
            break;
        }
    }
    
    // Free resources
    if (window->back_buffer) {
        KernelFree(window->back_buffer);
    }
    if (window->title) {
        KernelFree((void*)window->title);
    }
    KernelFree(window);
}

void WindowFill(Window* window, uint32_t color) {
    if (!window || !window->back_buffer) return;
    
    for (int i = 0; i < window->rect.width * window->rect.height; i++) {
        window->back_buffer[i] = color;
    }
    window->needs_redraw = true;
}

void WindowDrawRect(Window* window, int x, int y, int width, int height, uint32_t color) {
    if (!window || !window->back_buffer) return;
    
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && py >= 0 && px < window->rect.width && py < window->rect.height) {
                window->back_buffer[py * window->rect.width + px] = color;
            }
        }
    }
    window->needs_redraw = true;
}

void WindowDrawString(Window* window, int x, int y, const char* str, uint32_t fg_color) {
    if (!window || !window->back_buffer || !str) return;
    
    int start_x = x;
    while (*str) {
        if (*str == '\n') {
            y += FONT_HEIGHT;
            x = start_x;
        } else {
            unsigned char c = (unsigned char)*str;
            if (c < 256) {
                // Render character using font bitmap
                for (int dy = 0; dy < FONT_HEIGHT; dy++) {
                    unsigned char font_row = console_font[c][dy];
                    for (int dx = 0; dx < FONT_WIDTH; dx++) {
                        int px = x + dx;
                        int py = y + dy;
                        if (px >= 0 && py >= 0 && px < window->rect.width && py < window->rect.height) {
                            if (font_row & (0x80 >> dx)) {
                                window->back_buffer[py * window->rect.width + px] = fg_color;
                            }
                        }
                    }
                }
            }
            x += FONT_WIDTH;
        }
        str++;
    }
    window->needs_redraw = true;
}
// --- Input Event Handlers ---

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