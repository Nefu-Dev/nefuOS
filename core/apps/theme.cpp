// nefuOS Theme Settings
// Change accent color and UI theme
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../sys/settings.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

#define THEME_COUNT 5

struct ThemeOption {
    const char* name;
    int id;
    uint32_t color;
};

static ThemeOption themes[] = {
    { "Blue", 0, 0x1976D2 },
    { "Green", 1, 0x388E3C },
    { "Purple", 2, 0x7B1FA2 },
    { "Orange", 3, 0xF57C00 },
    { "Red", 4, 0xD32F2F },
};

void theme_launch() {
    Window* w = g_wm->create_window("Theme Settings", 100, 100, 480, 360);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x4A90D9);
    gfx::text(w->back, 12, 10, "Accent Color", 0xFFFFFF, 0x4A90D9);
    
    // Theme swatches
    int swatch_size = 64;
    int gap = 20;
    int start_x = 40;
    int start_y = 60;
    
    for (int i = 0; i < THEME_COUNT; i++) {
        int x = start_x + i * (swatch_size + gap);
        int y = start_y;
        
        // Color circle/square
        gfx::fillrect(w->back, x, y, swatch_size, swatch_size, themes[i].color);
        
        // Selection border
        if (g_settings.accent == themes[i].id) {
            gfx::rect(w->back, x - 4, y - 4, swatch_size + 8, swatch_size + 8, 0x333333);
            gfx::rect(w->back, x - 6, y - 6, swatch_size + 12, swatch_size + 12, 0x333333);
        } else {
            gfx::rect(w->back, x, y, swatch_size, swatch_size, 0x999999);
        }
        
        // Name below
        gfx::text(w->back, x + 10, y + swatch_size + 8, themes[i].name, 0x333333, 0xFFFFFF);
    }
    
    // Preview section
    int preview_y = 180;
    gfx::text(w->back, 20, preview_y, "Preview:", 0x1A1B1C, 0xFFFFFF);
    
    // Sample UI elements
    int pv_x = 20;
    int pv_y = preview_y + 28;
    
    // Sample button
    gfx::fillrect(w->back, pv_x, pv_y, 100, 32, themes[g_settings.accent].color);
    gfx::text(w->back, pv_x + 20, pv_y + 8, "Button", 0xFFFFFF, themes[g_settings.accent].color);
    
    // Sample window title bar
    gfx::fillrect(w->back, pv_x + 130, pv_y, 200, 32, themes[g_settings.accent].color);
    gfx::text(w->back, pv_x + 140, pv_y + 8, "Window Title", 0xFFFFFF, themes[g_settings.accent].color);
    
    // Sample taskbar
    gfx::fillrect(w->back, pv_x, pv_y + 50, 310, 32, 0x262A31);
    gfx::fillrect(w->back, pv_x + 4, pv_y + 54, 60, 24, themes[g_settings.accent].color);
    gfx::text(w->back, pv_x + 14, pv_y + 58, "Start", 0xFFFFFF, themes[g_settings.accent].color);
    
    // Info section
    int info_y = 280;
    gfx::fillrect(w->back, pv_x, info_y, w->content_w - 40, 50, 0xF5F5F5);
    gfx::rect(w->back, pv_x, info_y, w->content_w - 40, 50, 0xDDDDDD);
    gfx::text(w->back, pv_x + 12, info_y + 10, "Accent color affects buttons, title bars, and highlights.", 0x666666, 0xF5F5F5);
    gfx::text(w->back, pv_x + 12, info_y + 26, "Changes apply immediately to new windows.", 0x999999, 0xF5F5F5);
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 30, w->content_w, 30, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 22, "Click a color to change accent", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu
