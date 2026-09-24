// nefuOS Process List
// Shows running processes and resource usage
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {

// Mock process list for demonstration
struct MockProcess {
    char name[32];
    int pid;
    int cpu;
    int mem_kb;
};

static MockProcess processes[] = {
    { "nefuos", 1, 1, 4096 },
    { "desktop", 2, 2, 2048 },
    { "wm", 3, 1, 1024 },
    { "browser", 4, 15, 8192 },
    { "terminal", 5, 0, 512 },
    { "filemgr", 6, 1, 1024 },
    { "settings", 7, 0, 768 },
    { "inputmethod", 8, 0, 256 },
    { "clipboard", 9, 0, 128 },
    { "notifcenter", 10, 0, 256 },
    { "taskmgr", 11, 0, 384 },
    { "wiki", 12, 0, 1024 },
    { "music", 13, 2, 2048 },
    { "imageviewer", 14, 0, 1536 },
    { "weather", 15, 0, 384 },
};

#define PROCESS_COUNT (sizeof(processes) / sizeof(processes[0]))

void processlist_launch() {
    Window* w = g_wm->create_window("Process List", 60, 60, 520, 400);
    if (!w) return;
    
    // Header
    gfx::fillrect(w->back, 0, 0, w->content_w, 36, 0x263238);
    gfx::text(w->back, 12, 10, "Running Processes", 0xFFFFFF, 0x263238);
    
    // Column headers
    gfx::fillrect(w->back, 0, 40, w->content_w, 24, 0xCFD8DC);
    gfx::text(w->back, 12, 46, "Name", 0x263238, 0xCFD8DC);
    gfx::text(w->back, 180, 46, "PID", 0x263238, 0xCFD8DC);
    gfx::text(w->back, 260, 46, "CPU%", 0x263238, 0xCFD8DC);
    gfx::text(w->back, 360, 46, "Memory", 0x263238, 0xCFD8DC);
    
    // Process list
    int row_h = 20;
    int start_y = 70;
    
    int total_cpu = 0;
    int total_mem = 0;
    
    for (int i = 0; i < (int)PROCESS_COUNT; i++) {
        int y = start_y + i * row_h;
        
        if (y + row_h > w->content_h - 60) break;
        
        // Alternating row colors
        uint32_t bg = (i % 2 == 0) ? 0xFFFFFF : 0xF5F5F5;
        gfx::fillrect(w->back, 0, y, w->content_w, row_h, bg);
        
        // Process name
        gfx::text(w->back, 12, y + 3, processes[i].name, 0x1A1B1C, bg);
        
        // PID
        char pid_str[16];
        ksprintf(pid_str, 16, "%d", processes[i].pid);
        gfx::text(w->back, 180, y + 3, pid_str, 0x333333, bg);
        
        // CPU%
        char cpu_str[16];
        ksprintf(cpu_str, 16, "%d%%", processes[i].cpu);
        gfx::text(w->back, 260, y + 3, cpu_str, 
                 processes[i].cpu > 10 ? 0xD32F2F : 0x333333, bg);
        
        // Memory
        char mem_str[24];
        if (processes[i].mem_kb >= 1024) {
            ksprintf(mem_str, 24, "%d MB", processes[i].mem_kb / 1024);
        } else {
            ksprintf(mem_str, 24, "%d KB", processes[i].mem_kb);
        }
        gfx::text(w->back, 360, y + 3, mem_str, 0x333333, bg);
        
        total_cpu += processes[i].cpu;
        total_mem += processes[i].mem_kb;
    }
    
    // Summary bar
    int sum_y = w->content_h - 50;
    gfx::fillrect(w->back, 0, sum_y, w->content_w, 50, 0xECEFF1);
    gfx::rect(w->back, 0, sum_y, w->content_w, 50, 0xB0BEC5);
    
    char cpu_buf[32];
    ksprintf(cpu_buf, 32, "Total CPU: %d%%", total_cpu);
    gfx::text(w->back, 12, sum_y + 8, cpu_buf, 0x263238, 0xECEFF1);
    
    char mem_buf[32];
    if (total_mem >= 1024) {
        ksprintf(mem_buf, 32, "Total Memory: %d MB", total_mem / 1024);
    } else {
        ksprintf(mem_buf, 32, "Total Memory: %d KB", total_mem);
    }
    gfx::text(w->back, 180, sum_y + 8, mem_buf, 0x263238, 0xECEFF1);
    
    gfx::text(w->back, 12, sum_y + 28, "Processes: %d", 0x607D8B, 0xECEFF1);
    
    g_wm->raise(w);
}

} // namespace nefu
