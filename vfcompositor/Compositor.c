#include <Compositor.h>
#include <Console.h>
#include <Font.h>
#include <KernelHeap.h>
#include <Keyboard.h>
#include <MLFQ.h>
#include <MemOps.h>
#include <Pallete.h>
#include <Panic.h>
#include <PS2.h>
#include <Scheduler.h>
#include <Shell.h>
#include <SpinlockRust.h>
#include <StringOps.h>
#include <Vesa.h>
#include <app/GUIShell.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// --- Globals are now part of CompositorContext ---

CompositorContext g_compositor_ctx;

#define MOUSE_CURSOR_WIDTH 16
#define MOUSE_CURSOR_HEIGHT 16
const uint32_t mouse_cursor_bitmap[MOUSE_CURSOR_HEIGHT * MOUSE_CURSOR_WIDTH] = {
    0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // X
    0xFF000000, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWWWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWWWWW
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWWWX
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWWX
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWWX
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWWX
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWWX
    0xFF000000, 0xFFFFFFFF, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, // XWX
    0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000  // XX
};

void RequestDestroyWindow(CompositorContext* ctx, Window* w) {
    if (!w) return;
    // avoid duplicates
    for (int i = 0; i < ctx->g_pending_destroy_count; i++) if (ctx->g_pending_destroy[i] == w) return;
    if (ctx->g_pending_destroy_count < MAX_WINDOWS) ctx->g_pending_destroy[ctx->g_pending_destroy_count++] = w;
}

void VFCompositorRequestInit(const char * args) {
    char* is_fork = GetArg(args, 1);
    bool fork = false;

    if (FastStrCmp(is_fork, "fork") == 0 && is_fork) fork = true;
    else fork = false;

    KernelFree(is_fork);
#ifndef VF_CONFIG_ENABLE_VFCOMPOSITOR
    PrintKernelError("System: VFCompositor disabled in this build\n");
    return;
#endif
    Snooze();
    if (fork) {
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
    else VFCompositor();
}

void VFCompositorShutdown(CompositorContext* ctx) {
    if (ctx->g_compositor_buffer) {
        KernelFree(ctx->g_compositor_buffer);
        ctx->g_compositor_buffer = NULL;
    }
    if (ctx->g_text_lock) {
        rust_spinlock_free(ctx->g_text_lock);
        ctx->g_text_lock = NULL;
    }
}

// Get window by title
Window* GetWindowByTitle(CompositorContext* ctx, const char* title) {
    if (!title) return NULL;

    uint64_t flags = rust_spinlock_lock_irqsave(ctx->g_text_lock);

    Window* current = ctx->g_window_list_head;
    while (current) {
        if (current->title && FastStrCmp(current->title, title) == 0) {
            rust_spinlock_unlock_irqrestore(ctx->g_text_lock, flags);
            return current;
        }
        current = current->next;
    }

    rust_spinlock_unlock_irqrestore(ctx->g_text_lock, flags);
    return NULL;
}

static void DrawMouseCursor(CompositorContext* ctx) {
    if (!ctx->g_vbe_info || !ctx->g_compositor_buffer) return;

    for (int y = 0; y < MOUSE_CURSOR_HEIGHT; y++) {
        for (int x = 0; x < MOUSE_CURSOR_WIDTH; x++) {
            int screen_x = ctx->g_mouse_x + x;
            int screen_y = ctx->g_mouse_y + y;
            if (screen_x >= 0 && screen_y >= 0 && screen_x < (int)ctx->g_vbe_info->width && screen_y < (int)ctx->g_vbe_info->height) {
                uint32_t pixel_color = mouse_cursor_bitmap[y * MOUSE_CURSOR_WIDTH + x];
                if (pixel_color != 0x00000000) { // Assuming 0x00000000 is transparent
                    ctx->g_compositor_buffer[screen_y * ctx->g_vbe_info->width + screen_x] = pixel_color;
                }
            }
        }
    }
}

WindowTextState* GetWindowTextState(CompositorContext* ctx, Window* window) {
    if (!window) return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (ctx->g_window_state_map[i].in_use &&
            ctx->g_window_state_map[i].window == window) {
            return &ctx->g_window_state_map[i].state;
            }
    }
    // Allocate new state
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!ctx->g_window_state_map[i].in_use) {
            ctx->g_window_state_map[i].window       = window;
            ctx->g_window_state_map[i].in_use       = true;
            FastMemset(&ctx->g_window_state_map[i].state, 0, sizeof(WindowTextState));
            ctx->g_window_state_map[i].state.needs_refresh = true;
            return &ctx->g_window_state_map[i].state;
        }
    }
    return NULL;
}

// Initialize window for text mode
void WindowInitTextMode(CompositorContext* ctx, Window* window) {
    if (!window) return;

    WindowTextState* state = GetWindowTextState(ctx, window);
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
void WindowScrollUp(CompositorContext* ctx, Window* window) {
    WindowTextState* state = GetWindowTextState(ctx, window);
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
void WindowPrintChar(CompositorContext* ctx, Window* window, char c) {
    if (!window) return;

    uint64_t flags = rust_spinlock_lock_irqsave(ctx->g_text_lock);

    WindowTextState* state = GetWindowTextState(ctx, window);
    if (!state) {
        rust_spinlock_unlock_irqrestore(ctx->g_text_lock, flags);
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
        WindowScrollUp(ctx, window);
        state->cursor_row = WINDOW_TEXT_ROWS - 1;
    }

    state->needs_refresh = true;
    window->needs_redraw = true;

    rust_spinlock_unlock_irqrestore(ctx->g_text_lock, flags);
}

// Print string to window
void WindowPrintString(CompositorContext* ctx, Window* window, const char* str) {
    if (!window || !str) return;

    while (*str) {
        WindowPrintChar(ctx, window, *str);
        str++;
    }
}

// Clear window text
void WindowClearText(CompositorContext* ctx, Window* window) {
    WindowTextState* state = GetWindowTextState(ctx, window);
    if (!state) return;

    uint64_t flags = rust_spinlock_lock_irqsave(ctx->g_text_lock);

    FastMemset(state->buffer, 0, sizeof(state->buffer));
    state->cursor_row = 0;
    state->cursor_col = 0;
    state->needs_refresh = true;
    window->needs_redraw = true;

    rust_spinlock_unlock_irqrestore(ctx->g_text_lock, flags);
}

static void DrawTaskbar(CompositorContext* ctx) {
    if (!ctx->g_vbe_info) return;
    int y0 = ctx->g_vbe_info->height - TASKBAR_HEIGHT;
    // Background
    for (int y = y0; y < (int)ctx->g_vbe_info->height; y++) {
        for (int x = 0; x < (int)ctx->g_vbe_info->width; x++) {
            ctx->g_compositor_buffer[y * ctx->g_vbe_info->width + x] = TITLE_BAR;
        }
    }
    // Start button
    for (int y = 2; y < TASKBAR_HEIGHT - 2; y++) {
        for (int x = 2; x < START_BTN_WIDTH - 2; x++) {
            int px = x;
            int py = y0 + y;
            ctx->g_compositor_buffer[py * ctx->g_vbe_info->width + px] = ACCENT;
        }
    }
    // "Start" label
    int text_x = 10;
    int text_y = y0 + 6;
    const char* s = "Start";
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        for (int dy = 0; dy < FONT_HEIGHT && (text_y + dy) < (int)ctx->g_vbe_info->height; dy++) {
            unsigned char row = console_font[c][dy];
            for (int dx = 0; dx < FONT_WIDTH && (text_x + dx) < (int)ctx->g_vbe_info->width; dx++) {
                if (row & (0x80 >> dx)) {
                    ctx->g_compositor_buffer[(text_y + dy) * ctx->g_vbe_info->width + (text_x + dx)] = TERMINAL_TEXT;
                }
            }
        }
        text_x += FONT_WIDTH;
    }
    // Task buttons
    ctx->g_task_button_count = 0;
    int btn_x = START_BTN_WIDTH + 8;
    for (Window* w = ctx->g_window_list_head; w && ctx->g_task_button_count < MAX_TASK_BUTTONS; w = w->next) {
        TaskButton* b = &ctx->g_task_buttons[ctx->g_task_button_count++];
        b->x = btn_x; b->y = y0 + 4; b->w = 120; b->h = TASKBAR_HEIGHT - 8; b->win = w;
        for (int y = 0; y < b->h; y++) {
            for (int x = 0; x < b->w; x++) {
                int px = b->x + x;
                int py = b->y + y;
                if (px >= 0 && py >= 0 && px < (int)ctx->g_vbe_info->width && py < (int)ctx->g_vbe_info->height) {
                    uint32_t col = (w == ctx->g_focused_window) ? ACCENT : (w->minimized ? BORDER : TITLE_BAR);
                    ctx->g_compositor_buffer[py * ctx->g_vbe_info->width + px] = col;
                }
            }
        }
        if (w->title) {
            int tx = b->x + 6;
            int ty = b->y + 4;
            const char* p = w->title;
            int chars = (b->w - 12) / FONT_WIDTH;
            for (int i = 0; i < chars && *p; i++, p++) {
                unsigned char c = (unsigned char)*p;
                for (int dy = 0; dy < FONT_HEIGHT && (ty + dy) < (int)ctx->g_vbe_info->height; dy++) {
                    unsigned char row = console_font[c][dy];
                    for (int dx = 0; dx < FONT_WIDTH && (tx + dx) < (int)ctx->g_vbe_info->width; dx++) {
                        if (row & (0x80 >> dx)) {
                            ctx->g_compositor_buffer[(ty + dy) * ctx->g_vbe_info->width + (tx + dx)] = TERMINAL_TEXT;
                        }
                    }
                }
                tx += FONT_WIDTH;
            }
        }
        btn_x += b->w + 6;
    }
}

static void CompositeAndDraw(CompositorContext* ctx) {
    if (!ctx->g_vbe_info) return;

    uint32_t background_color = TERMINAL_BG;
    for (int i = 0; i < ctx->g_vbe_info->width * ctx->g_vbe_info->height; i++) {
        ctx->g_compositor_buffer[i] = background_color;
    }

    for (Window* win = ctx->g_window_list_head; win != NULL; win = win->next) {
        if (!win->back_buffer || win->minimized) continue;
        
        // Simple drop shadow
        int sx0 = MAX(0, win->rect.x + 3);
        int sy0 = MAX(0, win->rect.y + 3);
        int sx1 = MIN((int)ctx->g_vbe_info->width, win->rect.x + win->rect.width + 3);
        int sy1 = MIN((int)ctx->g_vbe_info->height, win->rect.y + win->rect.height + 3);
        for (int sy = sy0; sy < sy1; sy++) {
            for (int sx = sx0; sx < sx1; sx++) {
                ctx->g_compositor_buffer[sy * ctx->g_vbe_info->width + sx] = BORDER;
            }
        }
        
        // Blit window with clipping
        int src_y_start = MAX(0, -win->rect.y);
        int src_y_end = MIN(win->rect.height, (int)ctx->g_vbe_info->height - win->rect.y);
        int src_x_start = MAX(0, -win->rect.x);
        int src_x_end = MIN(win->rect.width, (int)ctx->g_vbe_info->width - win->rect.x);
        if (src_y_start >= src_y_end || src_x_start >= src_x_end) continue;
        for (int y = src_y_start; y < src_y_end; y++) {
            int screen_y = win->rect.y + y;
            if (screen_y < 0 || screen_y >= ctx->g_vbe_info->height) continue;
            int src_idx = y * win->rect.width + src_x_start;
            int dst_idx = screen_y * ctx->g_vbe_info->width + (win->rect.x + src_x_start);
            int copy_width = src_x_end - src_x_start;
            if (src_idx >= 0 && src_idx + copy_width <= win->rect.width * win->rect.height &&
                dst_idx >= 0 && dst_idx + copy_width <= ctx->g_vbe_info->width * ctx->g_vbe_info->height) {
                FastMemcpy(&ctx->g_compositor_buffer[dst_idx], &win->back_buffer[src_idx], copy_width * 4);
            }
        }
        // The needs_redraw flag is now only for internal window buffer updates, not for blitting to compositor buffer
        // win->needs_redraw = false; // Removed this line
    }

    DrawTaskbar(ctx);
    DrawMouseCursor(ctx);
    const uint32_t bpp   = ctx->g_vbe_info->bpp;
    const uint32_t pitch = ctx->g_vbe_info->pitch;
    if (bpp != 32 || pitch == 0) {
        return;
    }
    uint8_t* dst        = (uint8_t*)ctx->g_vbe_info->framebuffer;
    uint8_t* src        = (uint8_t*)ctx->g_compositor_buffer;
    const uint32_t row_bytes = ctx->g_vbe_info->width * 4;
    for (uint32_t row = 0; row < ctx->g_vbe_info->height; row++) {
        FastMemcpy(dst + row * pitch, src + row * row_bytes, row_bytes);
    }
}

void VFCompositor(void) {
    FastMemset(&g_compositor_ctx, 0, sizeof(CompositorContext));
    g_compositor_ctx.g_text_lock = rust_spinlock_new();
    if (!g_compositor_ctx.g_text_lock) {
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
    CompositorInit(&g_compositor_ctx);

    Window* w = CreateWindow(&g_compositor_ctx, 50, 50, 480, 360, "VFCompositor Help Menu", 0);
    if (w) {
        w->minimized = false;
        WindowFill(w, WINDOW_BG);
        WindowDrawRect(w, 0, 0, w->rect.width, 20, TITLE_BAR);
        WindowPrintString(&g_compositor_ctx, w, "[--- VoidFrame - VFCompositor ---]\n");
        WindowPrintString(&g_compositor_ctx, w, "[--- Version: v0.0.2-development4 ---]\n");
        WindowPrintString(&g_compositor_ctx, w, "CTRL + W: Closes active window\n");
        WindowPrintString(&g_compositor_ctx, w, "CTRL + M: Minimize active window\n");
        WindowPrintString(&g_compositor_ctx, w, "CTRL + L: Make the active window move with your mouse\n");
        WindowPrintString(&g_compositor_ctx, w, "CTRL + T: Creates new window\n");
        WindowPrintString(&g_compositor_ctx, w, "CTRL + S: Creates VFShell GUI\n");
        WindowPrintString(&g_compositor_ctx, w, "CTRL + <ESC>: Quit VFCompositor\n");
        WindowPrintString(&g_compositor_ctx, w, "ATL + <TAB>: Switches between window\n");
        WindowDrawRect(w, 0, 25, w->rect.width, w->rect.height - 25, TERMINAL_TEXT);
    }

    while (1) {
        if (VBEIsInitialized()) {
            // Render text content for all windows that need it
            if (HasInput()) {
                const char c = GetChar();
                if (c == PS2_CalcCombo(K_CTRL, 0x1B)) {
                    Unsnooze();
                    ClearScreen();
                    PrintKernelWarning("VFCompositor: exiting...\n");
                    VFCompositorShutdown(&g_compositor_ctx);
                    return;
                }
                if (c == PS2_CalcCombo(K_CTRL, 'T')) {
                    Window* w = CreateWindow(&g_compositor_ctx, 50, 50, 480, 360, "Window", 0);
                    w->minimized = false;
                    WindowFill(w, WINDOW_BG);
                    WindowPrintString(&g_compositor_ctx, w, "Blank window\n");
                } else if (c == PS2_CalcCombo(K_CTRL, 'W')) {
                    Window* w = g_compositor_ctx.g_focused_window; if (w) { RequestDestroyWindow(&g_compositor_ctx, w); }
                } else if (c == PS2_CalcCombo(K_CTRL, 'S')) {
                    CreateProcess("VFShellGUI", VFShellProcess);
                } else if (c == PS2_CalcCombo(K_CTRL, 'M')) {
                    Window* w = g_compositor_ctx.g_focused_window; if (w) { w->minimized = !w->minimized; }
                } else if (c == PS2_CalcCombo(K_CTRL, 'L')) {
                    Window* w = g_compositor_ctx.g_focused_window; if (w) { w->is_moving = true; w->move_offset_x = g_compositor_ctx.g_mouse_x - w->rect.x; }
                } else if (c == PS2_CalcCombo(K_SUPER, 'W')) {
                    Window* w = g_compositor_ctx.g_focused_window; if (w) { RequestDestroyWindow(&g_compositor_ctx, w); }
                } else if (c == PS2_CalcCombo(K_ALT, '\t')) {
                    Window* w = g_compositor_ctx.g_focused_window ? g_compositor_ctx.g_focused_window->next : g_compositor_ctx.g_window_list_head;
                    while (w && w->minimized) w = w->next;
                    if (!w) { w = g_compositor_ctx.g_window_list_head; while (w && w->minimized) w = w->next; }
                    if (w) g_compositor_ctx.g_focused_window = w;
                }
            }
            Window* current = g_compositor_ctx.g_window_list_head;
            while (current) {
                WindowTextState* state = GetWindowTextState(&g_compositor_ctx, current);
                if (state && state->needs_refresh) {
                    // Clear window and redraw title bar
                    WindowFill(current, WINDOW_BG);
                    uint32_t tb_col = (current == g_compositor_ctx.g_focused_window && !current->minimized) ? ACCENT : TITLE_BAR;
                    WindowDrawRect(current, 0, 0, current->rect.width, 20, tb_col);
                    if (current->title) {
                        WindowDrawString(current, 5, 2, current->title, TERMINAL_TEXT);
                    }
                    // Title bar controls
                    const int btn_size = 14; const int pad = 3;
                    int close_x = current->rect.width - pad - btn_size;
                    int min_x   = close_x - 2 - btn_size;
                    WindowDrawRect(current, min_x, pad, btn_size, btn_size, BORDER);
                    WindowDrawRect(current, close_x, pad, btn_size, btn_size, ERROR_COLOR);
                    WindowDrawChar(current, min_x + 3, pad, '-', TERMINAL_TEXT);
                    WindowDrawChar(current, close_x + 3, pad - 1, 'x', TERMINAL_TEXT);
                    
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
            
            // Process deferred destroys safely on compositor thread
            if (g_compositor_ctx.g_pending_destroy_count) {
                for (int i = 0; i < g_compositor_ctx.g_pending_destroy_count; i++) {
                    DestroyWindow(&g_compositor_ctx, g_compositor_ctx.g_pending_destroy[i]);
                }
                g_compositor_ctx.g_pending_destroy_count = 0;
            }
            CompositeAndDraw(&g_compositor_ctx);
        } else {
            Yield();
        }
    }

    Unsnooze();
}

// Window management functions
void CompositorInit(CompositorContext* ctx) {
    ctx->g_vbe_info = VBEGetInfo();
    if (!ctx->g_vbe_info) {
        PrintKernelError("WindowManager: Failed to get VBE info\n");
        return;
    }
    
    size_t buffer_size = ctx->g_vbe_info->width * ctx->g_vbe_info->height * sizeof(uint32_t);
    ctx->g_compositor_buffer = (uint32_t*)KernelMemoryAlloc(buffer_size);
    if (!ctx->g_compositor_buffer) {
        PrintKernelError("WindowManager: Failed to allocate compositor buffer\n");
        return;
    }
    
    FastMemset(ctx->g_compositor_buffer, 0, buffer_size);
    FastMemset(ctx->g_window_state_map, 0, sizeof(ctx->g_window_state_map));
    
    ctx->g_mouse_x = ctx->g_vbe_info->width / 2;
    ctx->g_mouse_y = ctx->g_vbe_info->height / 2;
}

Window* CreateWindow(CompositorContext* ctx, int x, int y, int width, int height, const char* title, uint32_t owner_pid) {
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
    window->minimized = false;
    window->owner_pid = owner_pid; // Store the owner PID
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
    if (!ctx->g_window_list_head) {
        ctx->g_window_list_head = window;
        ctx->g_window_list_tail = window;
    } else {
        ctx->g_window_list_tail->next = window;
        window->prev = ctx->g_window_list_tail;
        ctx->g_window_list_tail = window;
    }
    
    return window;
}

void DestroyWindow(CompositorContext* ctx, Window* window) {
    if (!window) return;
    bool was_focused = (ctx->g_focused_window == window);

    // Remove from window list
    if (window->prev) {
        window->prev->next = window->next;
    } else {
        ctx->g_window_list_head = window->next;
    }
    if (window->next) {
        window->next->prev = window->prev;
    } else {
        ctx->g_window_list_tail = window->prev;
    }

    // Remove from state map
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (ctx->g_window_state_map[i].in_use && ctx->g_window_state_map[i].window == window) {
            ctx->g_window_state_map[i].in_use = false;
            break;
        }
    }

    // Clear special pointers
    if (ctx->g_start_menu_window == window) {
        ctx->g_start_menu_window = NULL;
    }

    // Free resources
    if (window->back_buffer) KernelFree(window->back_buffer);
    if (window->title) KernelFree((void*)window->title);
    KernelFree(window);

    // Kill the owning process if it exists
    if (window->owner_pid != 0) {
        KillProcess(window->owner_pid);
    }

    // Reassign focus to a safe window
    if (was_focused) {
        ctx->g_focused_window = ctx->g_window_list_tail;
        while (ctx->g_focused_window && ctx->g_focused_window->minimized) {
            ctx->g_focused_window = ctx->g_focused_window->prev;
        }
    }
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

void WindowDrawChar(Window* window, int x, int y, char ch, uint32_t fg_color) {
    if (!window || !window->back_buffer) return;
    unsigned char c = (unsigned char)ch;
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
void OnMouseMove(CompositorContext* ctx, int x, int y, int dx, int dy) {
    if (!ctx->g_vbe_info) return;
    ctx->g_mouse_x = x;
    ctx->g_mouse_y = y;
    if (ctx->g_focused_window && ctx->g_focused_window->is_moving) {
        int new_x = ctx->g_focused_window->rect.x + dx;
        int new_y = ctx->g_focused_window->rect.y + dy;

        // Keep at least part of the window visible

        int min_visible = 20; // At least title bar should be visible
        if (new_x > (int)ctx->g_vbe_info->width - min_visible)
            new_x = ctx->g_vbe_info->width - min_visible;
        if (new_x < -(ctx->g_focused_window->rect.width - min_visible))
            new_x = -(ctx->g_focused_window->rect.width - min_visible);
        if (new_y > (int)ctx->g_vbe_info->height - min_visible)
            new_y = ctx->g_vbe_info->height - min_visible;
        if (new_y < 0)
            new_y = 0;

        ctx->g_focused_window->rect.x = new_x;
        ctx->g_focused_window->rect.y = new_y;
        ctx->g_focused_window->needs_redraw = true;
    }
}

void OnMouseButtonDown(CompositorContext* ctx, int x, int y, uint8_t button) {
    if (button != 1) return; // Only left button for now

    // Taskbar region
    if (ctx->g_vbe_info) {
        int taskbar_y0 = (int)ctx->g_vbe_info->height - TASKBAR_HEIGHT;
        if (y >= taskbar_y0) {
            // Start button
            if (x >= 2 && x < START_BTN_WIDTH - 2) {
                if (!ctx->g_start_menu_window) {
                    ctx->g_start_menu_window = CreateWindow(ctx, 2, taskbar_y0 - 200, 220, 180, "Start", 0);
                    if (ctx->g_start_menu_window) {
                        WindowFill(ctx->g_start_menu_window, WINDOW_BG);
                        WindowDrawRect(ctx->g_start_menu_window, 0, 0, ctx->g_start_menu_window->rect.width, 20, TITLE_BAR);
                        WindowDrawString(ctx->g_start_menu_window, 6, 2, "Start", TERMINAL_TEXT);
                        WindowDrawString(ctx->g_start_menu_window, 8, 30, "- Terminal", TERMINAL_TEXT);
                        WindowDrawString(ctx->g_start_menu_window, 8, 50, "- Editor", TERMINAL_TEXT);
                    }
                } else {
                    RequestDestroyWindow(ctx, ctx->g_start_menu_window);
                    ctx->g_start_menu_window = NULL;
                }
                return;
            }
            // Task buttons
            for (int i = 0; i < ctx->g_task_button_count; i++) {
                TaskButton* b = &ctx->g_task_buttons[i];
                if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
                    Window* top = b->win;
                    if (top) {
                        if (top->minimized) {
                            top->minimized = false;
                        } else if (ctx->g_focused_window == top) {
                            top->minimized = true;
                        } else {
                            ctx->g_focused_window = top;
                        }
                        // Bring to front
                        if (top != ctx->g_window_list_tail) {
                            if (top->prev) top->prev->next = top->next;
                            if (top->next) top->next->prev = top->prev;
                            if (ctx->g_window_list_head == top) ctx->g_window_list_head = top->next;
                            top->prev = ctx->g_window_list_tail;
                            top->next = NULL;
                            if (ctx->g_window_list_tail) ctx->g_window_list_tail->next = top;
                            ctx->g_window_list_tail = top;
                        }
                    }
                    return;
                }
            }
        }
    }

    // Find topmost window under cursor
    Window* top_window = NULL;
    for (Window* win = ctx->g_window_list_tail; win != NULL; win = win->prev) {
        if (win->minimized) continue;
        if (x >= win->rect.x && x < win->rect.x + win->rect.width &&
            y >= win->rect.y && y < win->rect.y + win->rect.height) {
            top_window = win; break;
        }
    }
    if (!top_window) return;

    // If focus is changing, mark old and new focused windows for redraw
    if (ctx->g_focused_window != top_window) {
        if (ctx->g_focused_window) {
            WindowTextState* old_state = GetWindowTextState(ctx, ctx->g_focused_window);
            if (old_state) old_state->needs_refresh = true;
            ctx->g_focused_window->needs_redraw = true;
        }
        WindowTextState* new_state = GetWindowTextState(ctx, top_window);
        if (new_state) new_state->needs_refresh = true;
        top_window->needs_redraw = true;
    }

    ctx->g_focused_window = top_window;
    if (top_window != ctx->g_window_list_tail) {
        if (top_window->prev) top_window->prev->next = top_window->next;
        if (top_window->next) top_window->next->prev = top_window->prev;
        if (ctx->g_window_list_head == top_window) ctx->g_window_list_head = top_window->next;
        top_window->prev = ctx->g_window_list_tail;
        top_window->next = NULL;
        if (ctx->g_window_list_tail) ctx->g_window_list_tail->next = top_window;
        ctx->g_window_list_tail = top_window;
    }

    // Title bar interactions
    int rel_x = x - top_window->rect.x;
    int rel_y = y - top_window->rect.y;
    if (rel_y < 20) {
        const int btn_size = 14; const int pad = 3;
        int close_x = top_window->rect.width - pad - btn_size;
        int min_x   = close_x - 2 - btn_size;
        if (rel_x >= close_x && rel_x < close_x + btn_size && rel_y >= pad && rel_y < pad + btn_size) {
            RequestDestroyWindow(ctx, top_window);
            return;
        }
        if (rel_x >= min_x && rel_x < min_x + btn_size && rel_y >= pad && rel_y < pad + btn_size) {
            top_window->minimized = true;
            return;
        }
        top_window->is_moving = true;
        return;
    }
}

void OnMouseButtonUp(CompositorContext* ctx, int x, int y, uint8_t button) {
    if (button == 1 && ctx->g_focused_window) { // Left button
        ctx->g_focused_window->is_moving = false;
    }
}
