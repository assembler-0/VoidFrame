#include <Compositor.h>
#include <Shell.h>
#include <app/GUIShell.h>
#include <Keyboard.h>
#include <Pallete.h>
#include <Scheduler.h>

static char command_buffer[256];
static int cmd_pos = 0;
extern char current_dir[256];

extern CompositorContext g_compositor_ctx;
extern void ConsoleSetWindowPrint(Window* w);

void VFShellProcess(void) {
    Window* w = CreateWindow(&g_compositor_ctx, 0, 0, 480, 360, "VFShell", GetCurrentProcess()->pid);
    if (!w) return;
    w->minimized = false;
    WindowFill(w, WINDOW_BG);
    WindowPrintString(&g_compositor_ctx, w, "[--- VFShell - GUI ---]\n/>");
    ConsoleSetWindowPrint(w);
    while (1) {
        if (HasInput()) {
            const char c = GetChar();

            if (c == '\n') {
                WindowPrintString(&g_compositor_ctx, w, "\n");
                command_buffer[cmd_pos] = 0;
                ExecuteCommand(command_buffer);
                cmd_pos = 0;
                WindowPrintString(&g_compositor_ctx, w, current_dir);
                WindowPrintString(&g_compositor_ctx, w,"> ");
            } else if (c == '\b') {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    WindowPrintString(&g_compositor_ctx, w, "\b \b"); // Visual backspace
                }
            } else if (cmd_pos < 255) {
                command_buffer[cmd_pos++] = c;
                const char str[2] = {c, 0};
                WindowPrintString(&g_compositor_ctx, w, str);
            }
        } else {
            Yield();
        }
    }
}
