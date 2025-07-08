#include "Driver.h"
#include "Io.h"
#include "../Core/Kernel.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

// Simple US keyboard layout
static char keymap[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static char key_buffer[256];
static int buffer_head = 0;
static int buffer_tail = 0;
static int cursor_line = 16;
static int cursor_col = 0;

static void KeyboardInit(void) {
    // Keyboard is already initialized by BIOS
    PrintKernelAt("Keyboard ready", 15, 0);
}

static void KeyboardInterrupt(uint8_t irq) {
    if (irq != 1) return; // IRQ1 = keyboard
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Only handle key press (not release)
    if (scancode & 0x80) return;
    
    char key = keymap[scancode];
    if (key) {
        // Add to buffer
        int next_head = (buffer_head + 1) % 256;
        if (next_head != buffer_tail) {
            key_buffer[buffer_head] = key;
            buffer_head = next_head;
            
            // Handle special keys
            if (key == '\n') {
                cursor_line++;
                cursor_col = 0;
                if (cursor_line >= 25) cursor_line = 24;
            } else if (key == '\b') {
                if (cursor_col > 0) {
                    cursor_col--;
                    PrintKernelAt(" ", cursor_line, cursor_col);
                }
            } else {
                char temp[2] = {key, 0};
                PrintKernelAt(temp, cursor_line, cursor_col);
                cursor_col++;
                if (cursor_col >= 80) {
                    cursor_col = 0;
                    cursor_line++;
                    if (cursor_line >= 25) cursor_line = 24;
                }
            }
        }
    }
}

static int KeyboardRead(void* buffer, uint32_t size) {
    char* buf = (char*)buffer;
    int count = 0;
    
    while (buffer_tail != buffer_head && count < size) {
        buf[count++] = key_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % 256;
    }
    
    return count;
}

// Keyboard driver instance
static Driver keyboard_driver = {
    .type = DRIVER_KEYBOARD,
    .name = "PS2 Keyboard",
    .init = KeyboardInit,
    .handle_interrupt = KeyboardInterrupt,
    .read = KeyboardRead,
    .write = 0 // Keyboard is input only
};

// Auto-register function
void KeyboardRegister(void) {
    DriverRegister(&keyboard_driver);
}