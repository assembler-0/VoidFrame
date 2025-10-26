#ifndef VF_PALLETE_H
#define VF_PALLETE_H

// =============================================================================
// PALETTE 1: "Deep Space" - Ultra dark with subtle blues
// =============================================================================
#define TERMINAL_BG_1       0x0A0A0A  // Almost black
#define TERMINAL_TEXT_1     0x4A9EFF  // Soft blue
#define WINDOW_BG_1         0x151515  // Very dark gray
#define TITLE_BAR_1         0x1A1A2E  // Dark blue-gray
#define BORDER_1            0x2A2A3A  // Subtle border
#define ACCENT_1            0x1D234A  // Bright blue accent
#define ERROR_1             0xFF6B6B  // Soft red
#define SUCCESS_1           0x51CF66  // Muted green

// =============================================================================
// PALETTE 2: "Tokyo Night" - Popular dark theme
// =============================================================================
#define TERMINAL_BG_2       0x1A1B26  // Deep dark blue
#define TERMINAL_TEXT_2     0x9AA5CE  // Light blue-gray
#define WINDOW_BG_2         0x24283B  // Dark purple-gray
#define TITLE_BAR_2         0x32344A  // Medium dark
#define BORDER_2            0x414868  // Visible border
#define ACCENT_2            0x7AA2F7  // Tokyo blue
#define ERROR_2             0xF7768E  // Pink error
#define SUCCESS_2           0x9ECE6A  // Green success

// =============================================================================
// PALETTE 3: "Dracula" - Classic vampire theme
// =============================================================================
#define TERMINAL_BG_3       0x282A36  // Dracula background
#define TERMINAL_TEXT_3     0xF8F8F2  // Off-white text
#define WINDOW_BG_3         0x44475A  // Dracula selection
#define TITLE_BAR_3         0x6272A4  // Purple-blue
#define BORDER_3            0x50FA7B  // Dracula green
#define ACCENT_3            0xFF79C6  // Dracula pink
#define ERROR_3             0xFF5555  // Dracula red
#define SUCCESS_3           0x50FA7B  // Dracula green

// =============================================================================
// PALETTE 4: "Pure Black Matrix" - Minimal black with green accents
// =============================================================================
#define TERMINAL_BG_4       0x000000  // Pure black
#define TERMINAL_TEXT_4     0x00AA00  // Matrix green (dimmer)
#define WINDOW_BG_4         0x0A0A0A  // Barely gray
#define TITLE_BAR_4         0x001100  // Very dark green tint
#define BORDER_4            0x003300  // Dim green border
#define ACCENT_4            0x00FF41  // Bright matrix green
#define ERROR_4             0xFF0000  // Pure red
#define SUCCESS_4           0x00FF00  // Bright green

// =============================================================================
// PALETTE 5: "Gruvbox Dark" - Retro terminal vibes
// =============================================================================
#define TERMINAL_BG_5       0x282828  // Gruvbox dark
#define TERMINAL_TEXT_5     0xEBDBB2  // Warm off-white
#define WINDOW_BG_5         0x3C3836  // Dark brown-gray
#define TITLE_BAR_5         0x504945  // Medium brown
#define BORDER_5            0x665C54  // Light brown
#define ACCENT_5            0xFE8019  // Orange accent
#define ERROR_5             0xFB4934  // Red
#define SUCCESS_5           0xB8BB26  // Olive green

// =============================================================================
// PALETTE 6: "Catppuccin Mocha" - Modern pastel dark
// =============================================================================
#define TERMINAL_BG_6       0x1E1E2E  // Base
#define TERMINAL_TEXT_6     0xCDD6F4  // Text
#define WINDOW_BG_6         0x313244  // Surface0
#define TITLE_BAR_6         0x45475A  // Surface1
#define BORDER_6            0x585B70  // Surface2
#define ACCENT_6            0x89B4FA  // Blue
#define ERROR_6             0xF38BA8  // Pink
#define SUCCESS_6           0xA6E3A1  // Green

// =============================================================================
// PALETTE 7: "Cyber Black" - Ultra minimal
// =============================================================================
#define TERMINAL_BG_7       0x000000  // Pure black
#define TERMINAL_TEXT_7     0x333333  // Very dark gray text
#define WINDOW_BG_7         0x0D0D0D  // Almost black
#define TITLE_BAR_7         0x1A1A1A  // Dark gray
#define BORDER_7            0x2A2A2A  // Subtle border
#define ACCENT_7            0x00FFFF  // Cyan accent
#define ERROR_7             0xFF3333  // Dim red
#define SUCCESS_7           0x33FF33  // Dim green

// =============================================================================
// PALETTE 8: "Nord Dark" - Arctic theme
// =============================================================================
#define TERMINAL_BG_8       0x2E3440  // Nord0
#define TERMINAL_TEXT_8     0xD8DEE9  // Nord4
#define WINDOW_BG_8         0x3B4252  // Nord1
#define TITLE_BAR_8         0x434C5E  // Nord2
#define BORDER_8            0x4C566A  // Nord3
#define ACCENT_8            0x88C0D0  // Nord8
#define ERROR_8             0xBF616A  // Nord11
#define SUCCESS_8           0xA3BE8C  // Nord14

// =============================================================================
// Easy palette switching system
// =============================================================================
#ifndef CURRENT_PALETTE
#define CURRENT_PALETTE 1
#endif

#if CURRENT_PALETTE == 1
    #define TERMINAL_BG     TERMINAL_BG_1
    #define TERMINAL_TEXT   TERMINAL_TEXT_1
    #define WINDOW_BG       WINDOW_BG_1
    #define TITLE_BAR       TITLE_BAR_1
    #define BORDER          BORDER_1
    #define ACCENT          ACCENT_1
    #define ERROR_COLOR     ERROR_1
    #define SUCCESS_COLOR   SUCCESS_1
#elif CURRENT_PALETTE == 2
    #define TERMINAL_BG     TERMINAL_BG_2
    #define TERMINAL_TEXT   TERMINAL_TEXT_2
    #define WINDOW_BG       WINDOW_BG_2
    #define TITLE_BAR       TITLE_BAR_2
    #define BORDER          BORDER_2
    #define ACCENT          ACCENT_2
    #define ERROR_COLOR     ERROR_2
    #define SUCCESS_COLOR   SUCCESS_2
#elif CURRENT_PALETTE == 3
    // Dracula
    #define TERMINAL_BG     TERMINAL_BG_3
    #define TERMINAL_TEXT   TERMINAL_TEXT_3
    #define WINDOW_BG       WINDOW_BG_3
    #define TITLE_BAR       TITLE_BAR_3
    #define BORDER          BORDER_3
    #define ACCENT          ACCENT_3
    #define ERROR_COLOR     ERROR_3
    #define SUCCESS_COLOR   SUCCESS_3
#elif CURRENT_PALETTE == 4
    // Pure Black Matrix
    #define TERMINAL_BG     TERMINAL_BG_4
    #define TERMINAL_TEXT   TERMINAL_TEXT_4
    #define WINDOW_BG       WINDOW_BG_4
    #define TITLE_BAR       TITLE_BAR_4
    #define BORDER          BORDER_4
    #define ACCENT          ACCENT_4
    #define ERROR_COLOR     ERROR_4
    #define SUCCESS_COLOR   SUCCESS_4
#elif CURRENT_PALETTE == 5
    // Gruvbox Dark
    #define TERMINAL_BG     TERMINAL_BG_5
    #define TERMINAL_TEXT   TERMINAL_TEXT_5
    #define WINDOW_BG       WINDOW_BG_5
    #define TITLE_BAR       TITLE_BAR_5
    #define BORDER          BORDER_5
    #define ACCENT          ACCENT_5
    #define ERROR_COLOR     ERROR_5
    #define SUCCESS_COLOR   SUCCESS_5
#elif CURRENT_PALETTE == 6
    // Catppuccin Mocha
    #define TERMINAL_BG     TERMINAL_BG_6
    #define TERMINAL_TEXT   TERMINAL_TEXT_6
    #define WINDOW_BG       WINDOW_BG_6
    #define TITLE_BAR       TITLE_BAR_6
    #define BORDER          BORDER_6
    #define ACCENT          ACCENT_6
    #define ERROR_COLOR     ERROR_6
    #define SUCCESS_COLOR   SUCCESS_6
#elif CURRENT_PALETTE == 7
    // Cyber Black
    #define TERMINAL_BG     TERMINAL_BG_7
    #define TERMINAL_TEXT   TERMINAL_TEXT_7
    #define WINDOW_BG       WINDOW_BG_7
    #define TITLE_BAR       TITLE_BAR_7
    #define BORDER          BORDER_7
    #define ACCENT          ACCENT_7
    #define ERROR_COLOR     ERROR_7
    #define SUCCESS_COLOR   SUCCESS_7
#else
    // Nord Dark (default fallback)
    #define TERMINAL_BG     TERMINAL_BG_8
    #define TERMINAL_TEXT   TERMINAL_TEXT_8
    #define WINDOW_BG       WINDOW_BG_8
    #define TITLE_BAR       TITLE_BAR_8
    #define BORDER          BORDER_8
    #define ACCENT          ACCENT_8
    #define ERROR_COLOR     ERROR_8
    #define SUCCESS_COLOR   SUCCESS_8
#endif

#endif