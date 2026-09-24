// nefuOS Wallpaper Settings
// Change desktop wallpaper and lock screen
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../sys/settings.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

#define WALLPAPER_COUNT 6

struct WallpaperOption {
    const char* name;
    int id;
    uint32_t color1;
    uint32_t color2;
};

static WallpaperOption wallpapers[] = {
    { "Blue Sky", 0, 0x1E88E5, 0x1565C0 },
    { "Sunset", 1, 0xFF7043, 0xBF360C },
    { "Dark", 2, 0x263238, 0x102027 },
    { "Green Forest", 3, 0x43A047, 0x1B5E20 },
    { "Purple", 4, 0x8E24AA, 0x4A148C },
    { "Orange", 5, 0xFB8C00, 0xE65100 },
};

// Paint function
static void paint_wallpaper(Window* w) {
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x4A90D9);
    gfx::text(w->back, 12, 10, "Desktop Wallpaper", 0xFFFFFF, 0x4A90D9);
    
    // Grid of wallpaper previews
    int grid_x = 20;
    int grid_y = 50;
    int preview_w = 140;
    int preview_h = 90;
    int gap = 16;
    int cols = 3;
    
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        
        int x = grid_x + col * (preview_w + gap);
        int y = grid_y + row * (preview_h + 40);
        
        // Preview box (gradient simulation)
        gfx::fillrect(w->back, x, y, preview_w, preview_h, wallpapers[i].color1);
        gfx::fillrect(w->back, x, y + preview_h / 2, preview_w, preview_h / 2, wallpapers[i].color2);
        
        // Border
        if (g_settings.wallpaper == wallpapers[i].id) {
            gfx::rect(w->back, x - 2, y - 2, preview_w + 4, preview_h + 4, 0x4A90D9);
            gfx::rect(w->back, x - 4, y - 4, preview_w + 8, preview_h + 8, 0x4A90D9);
        } else {
            gfx::rect(w->back, x, y, preview_w, preview_h, 0x999999);
        }
        
        // Name
        gfx::text(w->back, x + 10, y + preview_h + 8, wallpapers[i].name, 
                 g_settings.wallpaper == wallpapers[i].id ? 0x4A90D9 : 0x333333, 0xFFFFFF);
    }
    
    // Lock screen wallpaper section
    int lock_y = grid_y + 2 * (preview_h + 40) + 10;
    gfx::text(w->back, 20, lock_y, "Lock Screen Wallpaper:", 0x1A1B1C, 0xFFFFFF);
    
    for (int i = 0; i < 3; i++) {
        int x = 20 + i * 160;
        int y = lock_y + 24;
        
        gfx::fillrect(w->back, x, y, 140, 60, wallpapers[i].color1);
        gfx::fillrect(w->back, x, y + 30, 140, 30, wallpapers[i].color2);
        
        if (g_settings.wallpaper_lock == i) {
            gfx::rect(w->back, x - 2, y - 2, 144, 64, 0x4A90D9);
        }
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 30, w->content_w, 30, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 22, "Click a preview to change wallpaper", 0x666666, 0xF0F0F0);
}

// Mouse handler
static void on_wallpaper_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    
    int grid_x = 20;
    int grid_y = 50;
    int preview_w = 140;
    int preview_h = 90;
    int gap = 16;
    int cols = 3;
    
    // Check desktop wallpaper clicks
    for (int i = 0; i < WALLPAPER_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        
        int x = grid_x + col * (preview_w + gap);
        int y = grid_y + row * (preview_h + 40);
        
        if (mx >= x && mx <= x + preview_w && my >= y && my <= y + preview_h) {
            g_settings.wallpaper = wallpapers[i].id;
            paint_wallpaper(w);
            // Force desktop redraw via WM
            return;
        }
    }
    
    // Check lock screen wallpaper clicks
    int lock_y = grid_y + 2 * (preview_h + 40) + 10;
    for (int i = 0; i < 3; i++) {
        int x = 20 + i * 160;
        int y = lock_y + 24;
        
        if (mx >= x && mx <= x + 140 && my >= y && my <= y + 60) {
            g_settings.wallpaper_lock = i;
            paint_wallpaper(w);
            return;
        }
    }
}

void wallpaper_launch() {
    Window* w = g_wm->create_window("Wallpaper Settings", 80, 80, 520, 380);
    if (!w) return;
    
    paint_wallpaper(w);
    w->on_mouse = on_wallpaper_mouse;
    w->on_paint = paint_wallpaper;
    
    g_wm->raise(w);
}

} // namespace nefu
