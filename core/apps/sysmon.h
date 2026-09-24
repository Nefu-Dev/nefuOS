// nefuOS System Monitor Application
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu {
namespace apps {

struct SysMonState {
    int cpu_history[60];
    int mem_history[60];
    int history_idx;
    uint32_t start_tick;
    
    SysMonState() : history_idx(0) {
        for (int i = 0; i < 60; i++) {
            cpu_history[i] = 0;
            mem_history[i] = 0;
        }
        start_tick = platform_tick_ms();
    }
    
    void update() {
        // Simulate CPU usage (in real OS, read from /proc or kernel)
        uint32_t elapsed = platform_tick_ms() - start_tick;
        int cpu = (elapsed / 100) % 100;
        int mem = 30 + ((elapsed / 500) % 40);
        
        cpu_history[history_idx] = cpu;
        mem_history[history_idx] = mem;
        history_idx = (history_idx + 1) % 60;
    }
};

static void sysmon_paint(Window* w) {
    SysMonState* st = (SysMonState*)w->userdata;
    if (!st) return;
    
    st->update();
    
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x1E1E2E);
    
    // Title
    gfx::text(s, 10, 10, "System Monitor", 0xCdd6F4, 0x1E1E2E);
    
    // CPU Graph
    gfx::text(s, 10, 40, "CPU Usage", 0xA6E3A1, 0x1E1E2E);
    int graph_x = 10;
    int graph_y = 60;
    int graph_w = w->content_w - 20;
    int graph_h = 80;
    
    gfx::rect(s, graph_x, graph_y, graph_w, graph_h, 0x313244);
    
    for (int i = 0; i < 59; i++) {
        int idx1 = (st->history_idx + i) % 60;
        int idx2 = (st->history_idx + i + 1) % 60;
        int x1 = graph_x + (graph_w * i) / 59;
        int x2 = graph_x + (graph_w * (i + 1)) / 59;
        int y1 = graph_y + graph_h - (graph_h * st->cpu_history[idx1]) / 100;
        int y2 = graph_y + graph_h - (graph_h * st->cpu_history[idx2]) / 100;
        gfx::line(s, x1, y1, x2, y2, 0x89B4FA);
    }
    
    // Memory Graph
    gfx::text(s, 10, 160, "Memory Usage", 0xF9E2AF, 0x1E1E2E);
    graph_y = 180;
    
    gfx::rect(s, graph_x, graph_y, graph_w, graph_h, 0x313244);
    
    for (int i = 0; i < 59; i++) {
        int idx1 = (st->history_idx + i) % 60;
        int idx2 = (st->history_idx + i + 1) % 60;
        int x1 = graph_x + (graph_w * i) / 59;
        int x2 = graph_x + (graph_w * (i + 1)) / 59;
        int y1 = graph_y + graph_h - (graph_h * st->mem_history[idx1]) / 100;
        int y2 = graph_y + graph_h - (graph_h * st->mem_history[idx2]) / 100;
        gfx::line(s, x1, y1, x2, y2, 0xF5C2E7);
    }
    
    // Stats
    char stats[128];
    ksprintf(stats, sizeof(stats), "CPU: %d%%  |  MEM: %d%%  |  Uptime: %lus", 
             st->cpu_history[st->history_idx], 
             st->mem_history[st->history_idx],
             (platform_tick_ms() - st->start_tick) / 1000);
    gfx::text(s, 10, 280, stats, 0xCdd6F4, 0x1E1E2E);
}

Window* open_sysmon() {
    Window* w = g_wm->create_window("System Monitor", 150, 150, 400, 320);
    if (!w) return 0;
    
    SysMonState* st = new SysMonState();
    w->userdata = st;
    w->on_paint = sysmon_paint;
    
    return w;
}

} // namespace apps
} // namespace nefu
