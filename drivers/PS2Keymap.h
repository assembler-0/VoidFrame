#ifndef VOIDFRAME_PS2_KEYMAP_H
#define VOIDFRAME_PS2_KEYMAP_H

#include <stdint.h>

#define MAX_SCANCODE 128
#define MAX_KEYMAP_NAME 32

typedef struct {
    char name[MAX_KEYMAP_NAME];
    char normal[MAX_SCANCODE];
    char shift[MAX_SCANCODE];
} Keymap;

// Keymap management
void PS2_InitKeymaps(void);
int PS2_SetKeymap(const char* name);
const char* PS2_GetCurrentKeymapName(void);
void PS2_ListKeymaps(void);
int PS2_RegisterKeymap(const Keymap* keymap);

// Internal functions
char PS2_TranslateKey(uint8_t scancode, int shift_pressed);

#endif // VOIDFRAME_PS2_KEYMAP_H