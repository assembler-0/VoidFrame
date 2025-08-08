#include "Keyboard.h"
#include "Io.h"
#include "Console.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static char input_buffer[256];
static int buffer_head = 0;
static int buffer_tail = 0;
static int buffer_count = 0;

static char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

void KeyboardInit(void) {
    buffer_head = buffer_tail = buffer_count = 0;
}

void KeyboardHandler(void) {
    // Check if data is available
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return; // No data available
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    if (scancode & 0x80) return; // Key release
    if (scancode >= sizeof(scancode_to_ascii)) return; // Invalid scancode
    
    char c = scancode_to_ascii[scancode];
    if (c && buffer_count < 255) {
        input_buffer[buffer_tail] = c;
        buffer_tail = (buffer_tail + 1) % 256;
        buffer_count++;
    }
}

char GetChar(void) {
    if (buffer_count == 0) return 0;
    
    char c = input_buffer[buffer_head];
    buffer_head = (buffer_head + 1) % 256;
    buffer_count--;
    return c;
}

int HasInput(void) {
    return buffer_count > 0;
}