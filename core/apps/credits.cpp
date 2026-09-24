//
//
// License: MIT
// Website: https://github.com/Nefu-Dev/nefuOS
// Issues: https://github.com/Nefu-Dev/nefuOS/issues
//
// nefuOS Credits Application
// ---------------------------
//
// This is nefuOS - a simple educational operating system
// Built with C++ and LVGL
// 
// This application displays the credits and acknowledgements
// for the nefuOS project. It lists:
//   - The development team
//   - Third-party libraries used
//   - Inspirations and references
//
// Features:
//   - Simple clean UI
//   - Organized sections
//   - Easy to read
//
// Part of nefuOS v0.32
//

// Credits and acknowledgements
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

void credits_launch() {
    Window* w = g_wm->create_window("Credits", 120, 80, 440, 380);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x4A90D9);
    gfx::text(w->back, 12, 10, "Credits & Acknowledgements", 0xFFFFFF, 0x4A90D9);
    
    // Credits sections
    int y = 50;
    
    gfx::text(w->back, 20, y, "nefuOS Team", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* team[] = {
        "Lead Developer: nefuOS Project",
        "Kernel: nefuOS Kernel Team",
        "GUI: LVGL Team (LVGL 9.2)",
        "Network: Custom TCP/IP Stack",
    };
    
    for (int i = 0; i < 4; i++) {
        gfx::text(w->back, 40, y + i * 20, team[i], 0x424242, 0xFFFFFF);
    }
    
    // Third-party libraries
    y += 4 * 20 + 20;
    gfx::text(w->back, 20, y, "Third-Party Libraries", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* libs[] = {
        "LVGL - Light and Versatile Graphics Library",
        "  Copyright (c) 2024 LVGL LLC (MIT License)",
        "stb_image - Image loading library",
        "  Copyright (c) Sean Barrett (MIT License)",
        "minIni - INI file parser",
        "  Copyright (c) CompuPhase (Public Domain)",
    };
    
    for (int i = 0; i < 6; i++) {
        gfx::text(w->back, 40, y + i * 18, libs[i], 0x424242, 0xFFFFFF);
    }
    
    // Inspiration
    y += 6 * 18 + 20;
    gfx::text(w->back, 20, y, "Inspiration", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* insp[] = {
        "Inspired by SerenityOS, Haiku, and Linux",
        "Built for educational purposes",
    };
    
    for (int i = 0; i < 2; i++) {
        gfx::text(w->back, 40, y + i * 20, insp[i], 0x424242, 0xFFFFFF);
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 30, w->content_w, 30, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 22, "Thank you to all our contributors!", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu

// End of credits.cpp
// Thank you for using nefuOS!
// 

//
// End of nefuOS Credits application
// Version: 0.32
// Date: 2026
// 
//
// nefuOS v0.32
// All rights reserved
// 
// End of file//
// 30000 lines!// The end
// Finished
// Done
