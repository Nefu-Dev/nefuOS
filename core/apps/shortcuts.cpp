// nefuOS Shortcut Manager
// Shows and manages keyboard shortcuts
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

struct Shortcut {
    const char* keys;
    const char* action;
};

static Shortcut shortcuts[] = {
    { "Alt + Tab", "Switch windows" },
    { "Alt + F4", "Close window" },
    { "Win + D", "Show desktop" },
    { "Win + E", "Open File Manager" },
    { "Win + R", "Run dialog" },
    { "Win + L", "Lock screen" },
    { "Ctrl + C", "Copy" },
    { "Ctrl + V", "Paste" },
    { "Ctrl + X", "Cut" },
    { "Ctrl + Z", "Undo" },
    { "Ctrl + S", "Save" },
    { "Ctrl + O", "Open" },
    { "Ctrl + N", "New" },
    { "Ctrl + F", "Find" },
    { "Ctrl + A", "Select all" },
    { "Ctrl + B", "Bold" },
    { "F1", "Help" },
    { "F5", "Refresh" },
    { "PrtSc", "Screenshot" },
    { "Ctrl + Alt + T", "Open Terminal" },
};

#define SHORTCUT_COUNT (sizeof(shortcuts) / sizeof(shortcuts[0]))

void shortcut_launch() {
    Window* w = g_wm->create_window("Keyboard Shortcuts", 100, 60, 460, 440);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x4A90D9);
    gfx::text(w->back, 12, 10, "Keyboard Shortcuts", 0xFFFFFF, 0x4A90D9);
    
    // Column headers
    gfx::fillrect(w->back, 0, 40, w->content_w, 24, 0xF0F0F0);
    gfx::text(w->back, 16, 46, "Shortcut", 0x333333, 0xF0F0F0);
    gfx::text(w->back, 180, 46, "Action", 0x333333, 0xF0F0F0);
    
    // List shortcuts
    int row_h = 22;
    int start_y = 70;
    
    for (int i = 0; i < (int)SHORTCUT_COUNT; i++) {
        int y = start_y + i * row_h;
        
        if (y + row_h > w->content_h - 30) break;
        
        // Alternating row colors
        uint32_t bg = (i % 2 == 0) ? 0xFFFFFF : 0xF8F8F8;
        gfx::fillrect(w->back, 0, y, w->content_w, row_h, bg);
        
        // Key combination (in a little "key" box style)
        gfx::rect(w->back, 12, y + 3, 140, 16, 0xCCCCCC);
        gfx::text(w->back, 18, y + 5, shortcuts[i].keys, 0x1A1B1C, bg);
        
        // Action
        gfx::text(w->back, 180, y + 5, shortcuts[i].action, 0x333333, bg);
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 28, w->content_w, 28, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 20, "Press these keys to perform actions quickly", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu
