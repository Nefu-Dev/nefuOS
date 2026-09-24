// nefuOS System Info Extended
// Detailed system information display
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

void sysinfo_extended_launch() {
    Window* w = g_wm->create_window("System Information (Extended)", 80, 60, 520, 440);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x37474F);
    gfx::text(w->back, 12, 10, "System Information", 0xFFFFFF, 0x37474F);
    
    // CPU section
    int y = 50;
    gfx::text(w->back, 20, y, "CPU", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* cpu_info[] = {
        "Architecture: x86_64",
        "CPU Cores: 2 (SMP simulation)",
        "CPU Speed: 3.0 GHz (simulated)",
        "Cache: 8 MB L3 (simulated)",
    };
    
    for (int i = 0; i < 4; i++) {
        gfx::text(w->back, 40, y + i * 20, cpu_info[i], 0x424242, 0xFFFFFF);
    }
    
    // Memory section
    y += 4 * 20 + 20;
    gfx::text(w->back, 20, y, "Memory", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* mem_info[] = {
        "Total RAM: 1024 MB (limit)",
        "Used: 128 MB",
        "Free: 896 MB",
        "Swap: Not enabled",
    };
    
    for (int i = 0; i < 4; i++) {
        gfx::text(w->back, 40, y + i * 20, mem_info[i], 0x424242, 0xFFFFFF);
    }
    
    // Storage section
    y += 4 * 20 + 20;
    gfx::text(w->back, 20, y, "Storage", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* storage_info[] = {
        "Root FS: NVFS (nefu Virtual File System)",
        "Disk Size: 256 MB",
        "Used: 45 MB",
        "Free: 211 MB",
        "FAT32 Support: Enabled",
    };
    
    for (int i = 0; i < 5; i++) {
        gfx::text(w->back, 40, y + i * 20, storage_info[i], 0x424242, 0xFFFFFF);
    }
    
    // Network section
    y += 5 * 20 + 20;
    gfx::text(w->back, 20, y, "Network", 0x1A1B1C, 0xFFFFFF);
    y += 24;
    
    const char* net_info[] = {
        "Interface: Ethernet (simulated)",
        "IP Address: 192.168.1.100",
        "Subnet Mask: 255.255.255.0",
        "Gateway: 192.168.1.1",
    };
    
    for (int i = 0; i < 4; i++) {
        gfx::text(w->back, 40, y + i * 20, net_info[i], 0x424242, 0xFFFFFF);
    }
    
    // Footer
    gfx::fillrect(w->back, 0, w->content_h - 30, w->content_w, 30, 0xF0F0F0);
    gfx::text(w->back, 10, w->content_h - 22, "System information for nefuOS v0.32", 0x666666, 0xF0F0F0);
    
    g_wm->raise(w);
}

} // namespace nefu
