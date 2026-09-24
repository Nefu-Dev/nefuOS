// nefuOS UI Theme - Unified Design System
#pragma once

#include "gfx.h"

namespace nefu {
namespace ui {

// Color palette - nefuOS Design System
struct Theme {
    // Primary colors
    uint32_t primary;        // #3498DB
    uint32_t primary_dark;   // #2980B9
    uint32_t primary_light; // #85C1E9
    
    // Secondary colors
    uint32_t secondary;      // #2ECC71
    uint32_t secondary_dark;// #27AE60
    uint32_t secondary_light;// #82E0AA
    
    // Accent colors
    uint32_t accent;         // #E74C3C
    uint32_t accent_dark;   // #C0392B
    uint32_t accent_light;  // #F1948A
    
    // Neutral colors
    uint32_t background;    // #ECF0F1
    uint32_t surface;        // #FFFFFF
    uint32_t surface_dark;  // #BDC3C7
    uint32_t border;         // #95A5A6
    
    // Text colors
    uint32_t text_primary;   // #2C3E50
    uint32_t text_secondary; // #7F8C8D
    uint32_t text_light;     // #FFFFFF
    
    // Window chrome
    uint32_t titlebar;       // #2C3E50
    uint32_t titlebar_text;  // #ECF0F1
    uint32_t taskbar;        // #2C3E50
    
    // States
    uint32_t success;        // #27AE60
    uint32_t warning;        // #F39C12
    uint32_t error;          // #E74C3C
    uint32_t info;           // #3498DB
};

// Global theme instance
static Theme theme = {
    0x3498DB, 0x2980B9, 0x85C1E9,    // Primary
    0x2ECC71, 0x27AE60, 0x82E0AA,    // Secondary
    0xE74C3C, 0xC0392B, 0xF1948A,    // Accent
    0xECF0F1, 0xFFFFFF, 0xBDC3C7, 0x95A5A6,  // Neutral
    0x2C3E50, 0x7F8C8D, 0xFFFFFF,    // Text
    0x2C3E50, 0xECF0F1, 0x2C3E50,    // Window chrome
    0x27AE60, 0xF39C12, 0xE74C3C, 0x3498DB   // States
};

// Draw a modern button
inline void draw_button_modern(Surface& s, int x, int y, int w, int h, 
                               const char* text, bool hover = false, bool pressed = false) {
    uint32_t bg = hover ? theme.primary_dark : theme.primary;
    if (pressed) bg = theme.accent;
    
    // Button background
    gfx::fillrect(s, x, y, w, h, bg);
    
    // Button text
    int text_w = strlen(text) * 8;
    int text_x = x + (w - text_w) / 2;
    int text_y = y + (h - 16) / 2;
    gfx::text(s, text_x, text_y, text, theme.text_light, bg);
}

// Draw a modern input field
inline void draw_input(Surface& s, int x, int y, int w, int h, 
                       const char* placeholder, bool focused = false) {
    uint32_t bg = theme.surface;
    uint32_t border_color = focused ? theme.primary : theme.border;
    
    // Background
    gfx::fillrect(s, x, y, w, h, bg);
    
    // Border
    gfx::rect(s, x, y, w, h, border_color);
    
    // Text or placeholder
    if (strlen(placeholder) > 0) {
        gfx::text(s, x + 8, y + 6, placeholder, theme.text_secondary, bg);
    }
    
    // Cursor when focused
    if (focused) {
        int text_w = strlen(placeholder) * 8;
        gfx::fillrect(s, x + 8 + text_w, y + 4, 1, h - 8, theme.text_primary);
    }
}

// Draw a modern card
inline void draw_card(Surface& s, int x, int y, int w, int h) {
    // Background
    gfx::fillrect(s, x, y, w, h, theme.surface);
    
    // Border
    gfx::rect(s, x, y, w, h, theme.border);
}

// Draw a modern title bar
inline void draw_titlebar(Surface& s, int x, int y, int w, const char* title) {
    // Background
    gfx::fillrect(s, x, y, w, 24, theme.titlebar);
    
    // Title text
    gfx::text(s, x + 8, y + 5, title, theme.titlebar_text, theme.titlebar);
    
    // Window buttons
    int btn_size = 16;
    int btn_y = y + 4;
    
    // Close button
    gfx::fillrect(s, x + w - 20, btn_y, btn_size, btn_size, theme.accent);
    gfx::text(s, x + w - 16, btn_y + 2, "X", theme.text_light, theme.accent);
    
    // Maximize button
    gfx::fillrect(s, x + w - 40, btn_y, btn_size, btn_size, theme.warning);
    gfx::text(s, x + w - 36, btn_y + 2, "[]", theme.text_light, theme.warning);
    
    // Minimize button
    gfx::fillrect(s, x + w - 60, btn_y, btn_size, btn_size, theme.secondary);
    gfx::text(s, x + w - 56, btn_y + 2, "_", theme.text_light, theme.secondary);
}

// Draw a modern sidebar
inline void draw_sidebar(Surface& s, int x, int y, int w, int h) {
    gfx::fillrect(s, x, y, w, h, theme.titlebar);
}

// Draw a sidebar item
inline void draw_sidebar_item(Surface& s, int x, int y, int w, const char* label, bool selected = false) {
    uint32_t bg = selected ? theme.primary : theme.titlebar;
    gfx::fillrect(s, x, y, w, 28, bg);
    gfx::text(s, x + 12, y + 7, label, theme.titlebar_text, bg);
}

// Draw a status badge
inline void draw_badge(Surface& s, int x, int y, const char* text, uint32_t color) {
    int w = strlen(text) * 8 + 8;
    gfx::fillrect(s, x, y, w, 16, color);
    gfx::text(s, x + 4, y + 2, text, theme.text_light, color);
}

// Draw a progress bar
inline void draw_progressbar(Surface& s, int x, int y, int w, int h, int percent) {
    // Background
    gfx::fillrect(s, x, y, w, h, theme.surface_dark);
    
    // Fill
    int fill_w = (w * percent) / 100;
    gfx::fillrect(s, x, y, fill_w, h, theme.primary);
}

// Draw a notification
inline void draw_notification(Surface& s, int x, int y, int w, const char* title, const char* message) {
    // Background
    gfx::fillrect(s, x, y, w, 60, theme.surface);
    gfx::rect(s, x, y, w, 60, theme.border);
    
    // Accent border
    gfx::fillrect(s, x, y, 4, 60, theme.accent);
    
    // Title
    gfx::text(s, x + 12, y + 8, title, theme.text_primary, theme.surface);
    
    // Message
    gfx::text(s, x + 12, y + 28, message, theme.text_secondary, theme.surface);
}

} // namespace ui
} // namespace nefu
