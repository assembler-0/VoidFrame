#include "USBKeyboard.h"

#include "Console.h"
#include "VMem.h"

// Input buffer for the keyboard
static volatile char input_buffer[256];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;
static volatile int buffer_count = 0;

// Scancode to ASCII conversion tables (US QWERTY layout)
static const char scancode_to_ascii[] = {
    0,   0,   0,   0,   'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\n', 27,  '\b', '\t', ' ',
    '-', '=', '[', ']', '\\',  0,   ';', '\'', '`', ',', '.', '/', 0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char scancode_to_ascii_shift[] = {
    0,   0,   0,   0,   'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '\n', 27,  '\b', '\t', ' ',
    '_', '+', '{', '}', '|',   0,   ':', '"', '~', '<', '>', '?', 0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// This function will be called from the xHCI interrupt handler
void USBKeyboardHandleInput(USBHIDKeyboardReport* report) {
    // This is a simplified handler. A real implementation would handle
    // key releases and state changes more robustly.
    for (int i = 0; i < 6; i++) {
        uint8_t scancode = report->keycodes[i];
        if (scancode == 0) continue; // No key pressed in this slot

        char c = 0;
        if (scancode < sizeof(scancode_to_ascii)) {
            if (report->modifiers & 0x22) { // Check for Left or Right Shift
                c = scancode_to_ascii_shift[scancode];
            } else {
                c = scancode_to_ascii[scancode];
            }
        }

        if (c) {
            if (buffer_count < 255) {
                input_buffer[buffer_tail] = c;
                buffer_tail = (buffer_tail + 1) % 256;
                buffer_count++;
            }
        }
    }
}

void USBKeyboardInit(XhciController* controller, uint8_t slot_id) {
    PrintKernelSuccess("USBHID: Configuring USB keyboard on slot ");
    PrintKernelInt(slot_id);
    PrintKernel("\n");

    if (xHCIConfigureEndpoint(controller, slot_id) == 0) {
        PrintKernelSuccess("USBHID: USB keyboard configured and ready!\n");

        // Setup a buffer for the interrupt transfer
        USBHIDKeyboardReport* kbd_report = VMemAlloc(sizeof(USBHIDKeyboardReport));
        if (kbd_report) {
             // Start the first interrupt transfer to begin polling the keyboard
            xHCIInterruptTransfer(controller, slot_id, 1, kbd_report, sizeof(USBHIDKeyboardReport));
        } else {
            PrintKernelError("USBHID: Failed to allocate keyboard report buffer\n");
        }
    } else {
        PrintKernelError("USBHID: Failed to configure keyboard endpoint\n");
    }
}

char USB_Keyboard_GetChar(void) {
    if (buffer_count == 0) return 0;

    char c = input_buffer[buffer_head];
    buffer_head = (buffer_head + 1) % 256;
    buffer_count--;
    return c;
}

int USB_Keyboard_HasInput(void) {
    return buffer_count > 0;
}
