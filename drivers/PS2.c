#include "Io.h"
#include "PS2.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

static volatile char input_buffer[256];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static volatile int buffer_count = 0;

static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;

static char scancode_to_ascii[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static char scancode_to_ascii_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

void PS2Init(void) {
    outb(0x64, 0xAD);  // Disable keyboard
    outb(0x64, 0xA7);  // Disable mouse
    outb(0x64, 0xAE);  // Enable keyboard

    while (inb(0x64) & 0x01) {  // While data available
        inb(0x60);              // Read and discard
    }

    buffer_head = buffer_tail = buffer_count = 0;
}

void KeyboardHandler(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    int key_released = scancode & 0x80;
    scancode &= 0x7F;
    
    // Handle modifier keys
    if (scancode == 0x2A || scancode == 0x36) { // Left/Right Shift
        shift_pressed = !key_released;
        return;
    }
    if (scancode == 0x1D) { // Ctrl
        ctrl_pressed = !key_released;
        return;
    }
    if (scancode == 0x38) { // Alt
        alt_pressed = !key_released;
        return;
    }
    
    if (key_released) return;
    if (scancode >= sizeof(scancode_to_ascii)) return;
    
    char c;
    if (shift_pressed) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }
    
    // Handle Ctrl combinations
    if (ctrl_pressed && c >= 'a' && c <= 'z') {
        c = c - 'a' + 1; // Ctrl+A = 1, Ctrl+B = 2, etc.
    } else if (ctrl_pressed && c >= 'A' && c <= 'Z') {
        c = c - 'A' + 1;
    }
    
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