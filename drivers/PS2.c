#include "PS2.h"
#include "Io.h"
#include "Vesa.h"

// Keyboard buffer (unchanged)
static volatile char input_buffer[256];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static volatile int buffer_count = 0;

static int shift_pressed = 0;
static int ctrl_pressed = 0;
static int alt_pressed = 0;

// Mouse state
typedef struct {
    int x, y;
    int delta_x, delta_y;
    uint8_t buttons;
    int packet_index;
    uint8_t packet[3];
} MouseState;

static MouseState mouse = {0};

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

// Helper functions
static void wait_for_input_buffer_empty(void) {
    int timeout = 100000;
    while ((inb(KEYBOARD_STATUS_PORT) & 0x02) && --timeout);
}

static void wait_for_output_buffer_full(void) {
    int timeout = 100000;
    while (!(inb(KEYBOARD_STATUS_PORT) & 0x01) && --timeout);
}

void send_mouse_command(uint8_t cmd) {
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, PS2_CMD_WRITE_AUX);
    wait_for_input_buffer_empty();
    outb(KEYBOARD_DATA_PORT, cmd);
    wait_for_output_buffer_full();
    uint8_t response = inb(KEYBOARD_DATA_PORT);
    // Should be 0xFA (ACK) for most commands
}

void PS2Init(void) {
    // Flush the keyboard controller's buffer
    while (inb(KEYBOARD_STATUS_PORT) & 0x01) {
        inb(KEYBOARD_DATA_PORT);
    }

    // Disable both devices first
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, 0xAD); // Disable keyboard
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, PS2_CMD_DISABLE_AUX); // Disable mouse

    // Read current configuration
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, PS2_CMD_READ_CONFIG);
    wait_for_output_buffer_full();
    uint8_t config = inb(KEYBOARD_DATA_PORT);

    config |= 0x03;  // Enable keyboard and mouse interrupts

    // Write back configuration
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, PS2_CMD_WRITE_CONFIG);
    wait_for_input_buffer_empty();
    outb(KEYBOARD_DATA_PORT, config);

    // Enable auxiliary device
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, PS2_CMD_ENABLE_AUX);

    // Initialize mouse
    send_mouse_command(MOUSE_CMD_SET_DEFAULTS);
    send_mouse_command(MOUSE_CMD_ENABLE);

    // Re-enable keyboard
    wait_for_input_buffer_empty();
    outb(KEYBOARD_STATUS_PORT, 0xAE);

    // Reset keyboard
    wait_for_input_buffer_empty();
    outb(KEYBOARD_DATA_PORT, 0xFF);
    wait_for_output_buffer_full();
    uint8_t response = inb(KEYBOARD_DATA_PORT);
    if (response == 0xFA) {
        wait_for_output_buffer_full();
        inb(KEYBOARD_DATA_PORT); // Should be 0xAA
    }

    // Clear software state
    buffer_head = buffer_tail = buffer_count = 0;
    shift_pressed = ctrl_pressed = alt_pressed = 0;
    mouse.x = mouse.y = 0;
    mouse.packet_index = 0;
    mouse.buttons = 0;
}

void KeyboardHandler(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;

    // If bit 5 is set, the data is for the mouse.
    // In that case, the MouseHandler will deal with it, so we should ignore it.
    if (status & 0x20) {
        return;
    }

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
        c = c - 'a' + 1;
    } else if (ctrl_pressed && c >= 'A' && c <= 'Z') {
        c = c - 'A' + 1;
    }

    if (c) {
        if (OnKeyPress) {
            OnKeyPress(c);
        }
        if (buffer_count < 255) {
            input_buffer[buffer_tail] = c;
            buffer_tail = (buffer_tail + 1) % 256;
            buffer_count++;
        }
    }
}

void MouseHandler(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;

    // Check if this is mouse data (bit 5 set in status)
    if (!(status & 0x20)) return;

    uint8_t data = inb(KEYBOARD_DATA_PORT);

    mouse.packet[mouse.packet_index] = data;
    mouse.packet_index++;

    // Standard PS2 mouse sends 3-byte packets
    if (mouse.packet_index >= 3) {
        // Parse packet
        uint8_t flags = mouse.packet[0];
        int8_t delta_x = (int8_t)mouse.packet[1];
        int8_t delta_y = (int8_t)mouse.packet[2];

        // Check if packet is valid (bit 3 should be set)
        if (flags & 0x08) {
            // Handle X overflow
            if (flags & 0x40) {
                delta_x = (flags & 0x10) ? -256 : 255;
            } else if (flags & 0x10) {
                delta_x = (delta_x == 0) ? 0 : (delta_x | 0xFFFFFF00);
            }

            // Handle Y overflow
            if (flags & 0x80) {
                delta_y = (flags & 0x20) ? -256 : 255;
            } else if (flags & 0x20) {
                delta_y = (delta_y == 0) ? 0 : (delta_y | 0xFFFFFF00);
            }

            // Store previous button state to detect changes
            uint8_t old_buttons = mouse.buttons;

            // Update state
            mouse.x += delta_x;
            mouse.y -= delta_y;
            mouse.buttons = flags & 0x07;

            // Clamp position to screen resolution
            vbe_info_t* vbe = VBEGetInfo();
            if (vbe) {
                if (mouse.x < 0) mouse.x = 0;
                if (mouse.y < 0) mouse.y = 0;
                if (mouse.x >= (int)vbe->width) mouse.x = vbe->width - 1;
                if (mouse.y >= (int)vbe->height) mouse.y = vbe->height - 1;
            }

            // --- Fire Events ---
            if (OnMouseMove) {
                OnMouseMove(mouse.x, mouse.y, delta_x, delta_y);
            }

            // Check for button presses/releases
            uint8_t changed_buttons = mouse.buttons ^ old_buttons;
            if (changed_buttons) {
                for (int i = 0; i < 3; i++) {
                    uint8_t mask = 1 << i;
                    if (changed_buttons & mask) {
                        if ((mouse.buttons & mask) && OnMouseButtonDown) {
                            OnMouseButtonDown(mouse.x, mouse.y, i + 1);
                        } else if (OnMouseButtonUp) {
                            OnMouseButtonUp(mouse.x, mouse.y, i + 1);
                        }
                    }
                }
            }
        }

        mouse.packet_index = 0;
    }
}

// Existing keyboard functions (unchanged)
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

// New mouse functions
int GetMouseX(void) {
    return mouse.x;
}

int GetMouseY(void) {
    return mouse.y;
}

int GetMouseDeltaX(void) {
    int delta = mouse.delta_x;
    mouse.delta_x = 0;  // Reset after reading
    return delta;
}

int GetMouseDeltaY(void) {
    int delta = mouse.delta_y;
    mouse.delta_y = 0;  // Reset after reading
    return delta;
}

uint8_t GetMouseButtons(void) {
    return mouse.buttons;
}

int IsLeftButtonPressed(void) {
    return mouse.buttons & 0x01;
}

int IsRightButtonPressed(void) {
    return mouse.buttons & 0x02;
}

int IsMiddleButtonPressed(void) {
    return mouse.buttons & 0x04;
}