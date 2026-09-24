// nefuOS Release Notes
// Shows version history
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

void releasenotes_launch() {
    Window* w = g_wm->create_window("Release Notes", 100, 80, 460, 380);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x4A90D9);
    gfx::text(w->back, 12, 10, "Release Notes", 0xFFFFFF, 0x4A90D9);
    
    // Version history
    int y = 50;
    
    // v0.32
    gfx::text(w->back, 20, y, "Version 0.32", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* v032[] = {
        "- Added LVGL-based GUI",
        "- Real browser with TCP search",
        "- Input method (character palette)",
        "- Clipboard manager",
        "- Notification center",
        "- Shortcut reference",
        "- Wallpaper and theme settings",
        "- Process list view",
        "- 84 built-in applications",
    };
    
    for (int i = 0; i < 9; i++) {
        gfx::text(w->back, 40, y + i * 18, v032[i], 0x424242, 0xFFFFFF);
    }
    
    // v0.31
    y += 9 * 18 + 20;
    gfx::text(w->back, 20, y, "Version 0.31", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* v031[] = {
        "- Added window manager",
        "- File system improvements",
        "- Network stack updates",
    };
    
    for (int i = 0; i < 3; i++) {
        gfx::text(w->back, 40, y + i * 18, v031[i], 0x424242, 0xFFFFFF);
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 30, w->content_w, 30, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 22, "Latest updates for nefuOS", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu
