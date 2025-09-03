#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "Window.h"
#include "stdbool.h"
#include "stdint.h"

// Initializes the window manager and compositor
void WindowManagerInit(void);

// The main loop for the compositor, to be called repeatedly
void WindowManagerRun(void);

// Creates a new window
Window* CreateWindow(int x, int y, int width, int height, const char* title);

// Destroys a window
void DestroyWindow(Window* window);

// --- Drawing Primitives (operate on a window's backbuffer) ---

// Fills a window's backbuffer with a solid color
void WindowFill(Window* window, uint32_t color);

// Draws a rectangle within a window
void WindowDrawRect(Window* window, int x, int y, int width, int height, uint32_t color);

// Draws a string within a window
void WindowDrawString(Window* window, int x, int y, const char* str, uint32_t fg_color);

#endif // WINDOW_MANAGER_H
