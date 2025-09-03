#ifndef WINDOW_H
#define WINDOW_H

#include "stdbool.h"
#include "stdint.h"

// A simple rectangle structure
typedef struct {
    int x, y;
    int width, height;
} Rect;

// Forward declaration for the Window struct
typedef struct Window Window;

// Window structure
struct Window {
    Rect rect;
    const char* title;
    uint32_t* back_buffer; // Off-screen buffer for window content
    bool needs_redraw;

    // For window management (linked list)
    Window* next;
    Window* prev;

    // For window dragging
    bool is_moving;
    int move_offset_x;
    int move_offset_y;
};

#endif // WINDOW_H
