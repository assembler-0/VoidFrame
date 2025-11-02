#include <PS2Keymap.h>
#include <Console.h>
#include <MemOps.h>
#include <StringOps.h>

#define MAX_KEYMAPS 8

static Keymap keymaps[MAX_KEYMAPS];
static int keymap_count = 0;
static int current_keymap = 0;

// US QWERTY layout
static const Keymap us_qwerty = {
    .name = "us_qwerty",
    .normal = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    },
    .shift = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0, ' '
    }
};

static const Keymap us_qwertz = {
    .name = "us_qwertz",
    .normal = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        0, '\\', 'y', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*', 0, ' '
    },
    .shift = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        0, '|', 'Y', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
        '*', 0, ' '
    }
};

// Dvorak layout
static const Keymap dvorak = {
    .name = "dvorak",
    .normal = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '[', ']', '\b',
        '\t', '\'', ',', '.', 'p', 'y', 'f', 'g', 'c', 'r', 'l', '/', '=', '\n',
        0, 'a', 'o', 'e', 'u', 'i', 'd', 'h', 't', 'n', 's', '-', '`',
        0, '\\', ';', 'q', 'j', 'k', 'x', 'b', 'm', 'w', 'v', 'z', 0,
        '*', 0, ' '
    },
    .shift = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '{', '}', '\b',
        '\t', '"', '<', '>', 'P', 'Y', 'F', 'G', 'C', 'R', 'L', '?', '+', '\n',
        0, 'A', 'O', 'E', 'U', 'I', 'D', 'H', 'T', 'N', 'S', '_', '~',
        0, '|', ':', 'Q', 'J', 'K', 'X', 'B', 'M', 'W', 'V', 'Z', 0,
        '*', 0, ' '
    }
};

void PS2_InitKeymaps(void) {
    keymap_count = 0;
    PS2_RegisterKeymap(&us_qwerty);
    PS2_RegisterKeymap(&dvorak);
    PS2_RegisterKeymap(&us_qwertz);
    current_keymap = 0;
    PrintKernelSuccess("PS2: Initialized keymaps (default: us)\n");
}

int PS2_RegisterKeymap(const Keymap* keymap) {
    if (keymap_count >= MAX_KEYMAPS) return -1;
    
    FastMemcpy(&keymaps[keymap_count], keymap, sizeof(Keymap));
    keymap_count++;
    return 0;
}

int PS2_SetKeymap(const char* name) {
    for (int i = 0; i < keymap_count; i++) {
        if (FastStrCmp(keymaps[i].name, name) == 0) {
            current_keymap = i;
            PrintKernelF("PS2: Switched to keymap: %s\n", name);
            return 0;
        }
    }
    return -1;
}

const char* PS2_GetCurrentKeymapName(void) {
    if (current_keymap < keymap_count) {
        return keymaps[current_keymap].name;
    }
    return "unknown";
}

void PS2_ListKeymaps(void) {
    PrintKernel("Available keymaps:\n");
    for (int i = 0; i < keymap_count; i++) {
        PrintKernelF("  %s%s\n", keymaps[i].name,
                   (i == current_keymap) ? " (current)" : "");
    }
}

char PS2_TranslateKey(uint8_t scancode, int shift_pressed) {
    if (current_keymap >= keymap_count || scancode >= MAX_SCANCODE) {
        return 0;
    }
    
    return shift_pressed ? keymaps[current_keymap].shift[scancode]
                        : keymaps[current_keymap].normal[scancode];
}