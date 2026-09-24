// nefuOS About Dialog
// Shows system information and credits
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

void about_launch_full() {
    Window* w = g_wm->create_window("About nefuOS", 100, 80, 460, 420);
    if (!w) return;
    
    // Header with logo area
    gfx::fillrect(w->back, 0, 0, w->content_w, 100, 0x1976D2);
    
    // "Logo" text
    gfx::text(w->back, 140, 20, "nefuOS", 0xFFFFFF, 0x1976D2);
    gfx::text(w->back, 130, 44, "Version 0.32", 0xBBDEFB, 0x1976D2);
    gfx::text(w->back, 120, 68, "A simple C++ operating system", 0xBBDEFB, 0x1976D2);
    
    // System info section
    int info_y = 120;
    gfx::text(w->back, 20, info_y, "System Information", 0x1A1B1C, 0xFFFFFF);
    
    // Info rows
    struct InfoRow { const char* label; const char* value; };
    InfoRow rows[] = {
        { "OS Name:", "nefuOS" },
        { "Version:", "0.32 (Build 3200)" },
        { "Architecture:", "x86_64" },
        { "Kernel:", "nefu kernel 1.0" },
        { "GUI:", "LVGL based" },
        { "Shell:", "nefu Shell" },
        { "File System:", "NVFS + FAT32" },
        { "Compiler:", "GCC 13.2 (MinGW)" },
    };
    
    int row_h = 24;
    for (int i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++) {
        int y = info_y + 30 + i * row_h;
        gfx::text(w->back, 20, y, rows[i].label, 0x6B7280, 0xFFFFFF);
        gfx::text(w->back, 140, y, rows[i].value, 0x1A1B1C, 0xFFFFFF);
    }
    
    // Features section
    int feat_y = 280;
    gfx::text(w->back, 20, feat_y, "Features", 0x1A1B1C, 0xFFFFFF);
    
    const char* features[] = {
        "- Window Manager with LVGL",
        "- Built-in Applications",
        "- File System (NVFS)",
        "- TCP/IP Network Stack",
        "- Real Browser with Search",
        "- Taskbar and Start Menu",
    };
    
    for (int i = 0; i < (int)(sizeof(features) / sizeof(features[0])); i++) {
        int y = feat_y + 24 + i * 18;
        gfx::text(w->back, 30, y, features[i], 0x424242, 0xFFFFFF);
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 32, w->content_w, 32, 0xF5F5F5);
    gfx::text(w->back, 20, w->content_h - 22, "Copyright (c) 2024-2026 nefuOS Project", 0x757575, 0xF5F5F5);
    gfx::text(w->back, 20, w->content_h - 12, "Licensed under MIT License", 0x9E9E9E, 0xF5F5F5);
    
    g_wm->raise(w);
}

} // namespace nefu
