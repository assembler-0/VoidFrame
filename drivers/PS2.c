#include "PS2.h"

#include "APIC/APIC.h"
#include "Console.h"
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

// Compute result of a modifier combo with a base character.
char PS2_CalcCombo(uint8_t mods, char base) {
    char c = base;
    if (mods & K_SHIFT) {
        // Uppercase letters on shift if base is lowercase.
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    }
    if (mods & K_CTRL) {
        c = PS2_Ctrl(c);
    }
    return c;
}

static int wait_for_input_buffer_empty(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(KEYBOARD_STATUS_PORT) & 0x02)) return 0;
        __asm__ __volatile__("pause");
    }
    return -1;
}
static int wait_for_output_buffer_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) return 0;
        __asm__ __volatile__("pause");
    }
    return -1;
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
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (status & 0xC0) {  // Bits 6-7: timeout/parity errors
        PrintKernelWarning("PS2: Controller errors detected, performing reset\n");
    }

    // More aggressive buffer flushing
    int flush_attempts = 0;
    while ((inb(KEYBOARD_STATUS_PORT) & 0x01) && flush_attempts++ < 32) {
        inb(KEYBOARD_DATA_PORT);
        // Small delay to let controller settle
        for (volatile int i = 0; i < 1000; i++);
    }
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

    PrintKernel("Unmasking PS/2 driver IRQs\n");
    ApicEnableIrq(1);
    ApicEnableIrq(12);
    PrintKernelSuccess("PS/2 driver IRQs unmaked\n");
}

static void ProcessKeyboardData(uint8_t scancode) {
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

    char base = shift_pressed ? scancode_to_ascii_shift[scancode]
                              : scancode_to_ascii[scancode];

    uint8_t mods = (shift_pressed ? K_SHIFT : 0) |
                   (ctrl_pressed  ? K_CTRL  : 0) |
                   (alt_pressed   ? K_ALT   : 0);

    char c = PS2_CalcCombo(mods, base);

    if (c) {
        if (buffer_count < 255) {
            input_buffer[buffer_tail] = c;
            buffer_tail = (buffer_tail + 1) % 256;
            buffer_count++;
        }
    }
}

static void ProcessMouseData(uint8_t data) {
    // This logic is now inside a dedicated function.
    // It's only called when we are sure we have mouse data.
    mouse.packet[mouse.packet_index] = data;
    mouse.packet_index++;

    if (mouse.packet_index >= 3) {
        mouse.packet_index = 0; // Reset early to prevent re-entry issues
        uint8_t flags = mouse.packet[0];

        // Basic validation: bit 3 must be 1
        if (!(flags & 0x08)) {
            return;
        }

        int16_t delta_x = mouse.packet[1];
        int16_t delta_y = mouse.packet[2];

        if (flags & 0x10) delta_x |= 0xFF00; // X sign bit
        if (flags & 0x20) delta_y |= 0xFF00; // Y sign bit

        uint8_t old_buttons = mouse.buttons;
        mouse.buttons = flags & 0x07;

        mouse.x += delta_x;
        mouse.y -= delta_y; // PS/2 mouse Y-axis is inverted
        mouse.delta_x += delta_x;
        mouse.delta_y -= delta_y;

        vbe_info_t* vbe = VBEGetInfo();
        if (vbe) {
            if (mouse.x < 0) mouse.x = 0;
            if (mouse.y < 0) mouse.y = 0;
            if (mouse.x >= (int)vbe->width) mouse.x = vbe->width - 1;
            if (mouse.y >= (int)vbe->height) mouse.y = vbe->height - 1;
        }

        if (OnMouseMove) {
            OnMouseMove(mouse.x, mouse.y, delta_x, -delta_y);
        }

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
}

// Unified PS/2 Interrupt Handler
void PS2Handler(void) {
    uint8_t status;

    // Drain the controller's output buffer completely to avoid losing IRQ edges
    // If we return while the buffer is still full, additional edges may not fire
    // (since the 8042 only toggles the IRQ line on transitions to non-empty).
    while (1) {
        status = inb(KEYBOARD_STATUS_PORT);
        if (!(status & 0x01)) {
            break; // No more data
        }

        uint8_t data = inb(KEYBOARD_DATA_PORT);

        if (status & 0x20) {
            ProcessMouseData(data);
        } else {
            ProcessKeyboardData(data);
        }
    }
}


// Existing keyboard functions (unchanged)
char PS2_GetChar(void) {
    if (buffer_count == 0) return 0;

    char c = input_buffer[buffer_head];
    buffer_head = (buffer_head + 1) % 256;
    buffer_count--;
    return c;
}

int PS2_HasInput(void) {
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