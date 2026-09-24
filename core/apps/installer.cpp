// nefuOS Installer
// First-run setup wizard: choose install type (single system / dual boot ESP)
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"
#include "../sys/settings.h"

namespace nefu {

#define INSTALL_SINGLE  0  // Erase entire disk, only nefuOS
#define INSTALL_ESP     1  // Dual boot, ESP multi-partition

static int s_selected_option = INSTALL_SINGLE;

void installer_launch() {
    // Fullscreen installer window
    Window* w = g_wm->create_window("nefuOS Setup", 0, 0, 800, 600);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 80, 0x1976D2);
    gfx::text(w->back, 30, 20, "Welcome to nefuOS Setup", 0xFFFFFF, 0x1976D2);
    gfx::text(w->back, 30, 48, "Choose installation type", 0xBBDEFB, 0x1976D2);
    
    // Option 1: Single system (erase disk)
    int opt1_y = 120;
    if (s_selected_option == INSTALL_SINGLE) {
        gfx::fillrect(w->back, 40, opt1_y, 720, 140, 0xE3F2FD);
        gfx::rect(w->back, 40, opt1_y, 720, 140, 0x1976D2);
        gfx::rect(w->back, 42, opt1_y + 2, 716, 136, 0x1976D2);
    } else {
        gfx::rect(w->back, 40, opt1_y, 720, 140, 0x90A4AE);
    }
    
    gfx::text(w->back, 60, opt1_y + 16, "Single System (Erase Disk)", 
             s_selected_option == INSTALL_SINGLE ? 0x1976D2 : 0x1A1B1C, 
             s_selected_option == INSTALL_SINGLE ? 0xE3F2FD : 0xFFFFFF);
    
    gfx::text(w->back, 60, opt1_y + 44, "This will erase the entire disk and install nefuOS as the only system.", 
             0x424242, s_selected_option == INSTALL_SINGLE ? 0xE3F2FD : 0xFFFFFF);
    gfx::text(w->back, 60, opt1_y + 64, "All data on the disk will be lost.", 
             0xD32F2F, s_selected_option == INSTALL_SINGLE ? 0xE3F2FD : 0xFFFFFF);
    gfx::text(w->back, 60, opt1_y + 92, "Recommended for: new machines, dedicated devices", 
             0x6B7280, s_selected_option == INSTALL_SINGLE ? 0xE3F2FD : 0xFFFFFF);
    
    // Option 2: Dual boot (ESP multi-partition)
    int opt2_y = 280;
    if (s_selected_option == INSTALL_ESP) {
        gfx::fillrect(w->back, 40, opt2_y, 720, 140, 0xE8F5E9);
        gfx::rect(w->back, 40, opt2_y, 720, 140, 0x388E3C);
        gfx::rect(w->back, 42, opt2_y + 2, 716, 136, 0x388E3C);
    } else {
        gfx::rect(w->back, 40, opt2_y, 720, 140, 0x90A4AE);
    }
    
    gfx::text(w->back, 60, opt2_y + 16, "Dual Boot (ESP Multi-Partition)", 
             s_selected_option == INSTALL_ESP ? 0x388E3C : 0x1A1B1C, 
             s_selected_option == INSTALL_ESP ? 0xE8F5E9 : 0xFFFFFF);
    
    gfx::text(w->back, 60, opt2_y + 44, "Install nefuOS alongside existing Windows (dual boot).", 
             0x424242, s_selected_option == INSTALL_ESP ? 0xE8F5E9 : 0xFFFFFF);
    gfx::text(w->back, 60, opt2_y + 64, "Creates ESP partition and adds nefuOS to boot menu.", 
             0x424242, s_selected_option == INSTALL_ESP ? 0xE8F5E9 : 0xFFFFFF);
    gfx::text(w->back, 60, opt2_y + 92, "Recommended for: keeping Windows alongside nefuOS", 
             0x6B7280, s_selected_option == INSTALL_ESP ? 0xE8F5E9 : 0xFFFFFF);
    
    // Buttons
    int btn_y = 460;
    
    // Cancel button
    gfx::rect(w->back, 100, btn_y, 160, 40, 0x90A4AE);
    gfx::text(w->back, 150, btn_y + 12, "Cancel", 0x424242, 0xFFFFFF);
    
    // Install button
    gfx::fillrect(w->back, 540, btn_y, 160, 40, 0x1976D2);
    gfx::text(w->back, 585, btn_y + 12, "Install", 0xFFFFFF, 0x1976D2);
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 40, w->content_w, 40, 0xF5F5F5);
    gfx::text(w->back, 30, w->content_h - 26, "Use arrow keys to select, Enter to confirm", 0x6B7280, 0xF5F5F5);
    
    g_wm->raise(w);
}

} // namespace nefu
