// nefuOS System Info App
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"
#include "../platform.h"
#include <cstring>

namespace nefu {

namespace {

struct SysInfoState {
    int y;
};

static void sysinfo_paint(Window* w) {
    SysInfoState* st = (SysInfoState*)w->userdata;
    Surface& s = w->back;

    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xFFFFFF);

    int y = 10;
    int lh = 20;

    // Title
    gfx::text(s, 10, y, "nefuOS System Information", 0x000000, 0xFFFFFF);
    y += lh * 2;

    // OS info
    gfx::text(s, 10, y, "OS: nefuOS v3.0 (Lavender)", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 10, y, "Kernel: x86_64 multiboot", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 10, y, "GUI: LVGL 9.2 + custom renderer", 0x333333, 0xFFFFFF);
    y += lh * 2;

    // Memory
    gfx::text(s, 10, y, "Memory:", 0x000000, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Total RAM: ~64 MB", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Heap: 64 MB (0x400000)", 0x333333, 0xFFFFFF);
    y += lh * 2;

    // Storage
    gfx::text(s, 10, y, "Storage:", 0x000000, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "VFS: In-memory + FAT32", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "NVFS: nefu virtual FS", 0x333333, 0xFFFFFF);
    y += lh * 2;

    // Display
    gfx::text(s, 10, y, "Display:", 0x000000, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Resolution: 800x600", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Color depth: 32-bit", 0x333333, 0xFFFFFF);
    y += lh * 2;

    // Network
    gfx::text(s, 10, y, "Network:", 0x000000, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "HTTP client (WinINet / raw TCP)", 0x333333, 0xFFFFFF);
    y += lh * 2;

    // Apps
    gfx::text(s, 10, y, "Applications:", 0x000000, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Browser / Terminal / File Manager", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Settings / Store / Image Viewer", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Music Player / System Monitor", 0x333333, 0xFFFFFF);
    y += lh;
    gfx::text(s, 20, y, "Calculator / Calendar / Snake", 0x333333, 0xFFFFFF);
    y += lh * 2;

    // License
    gfx::text(s, 10, y, "License: MIT / BSD (see README)", 0x666666, 0xFFFFFF);
}

static void sysinfo_close(Window* w) {
    SysInfoState* st = (SysInfoState*)w->userdata;
    delete st;
}

} // namespace

void sysinfo_launch() {
    SysInfoState* st = new SysInfoState();
    Window* w = g_wm->create_window("System Info", 200, 150, 350, 450);
    w->userdata = st;
    w->on_paint = sysinfo_paint;
    w->on_close = sysinfo_close;
    g_wm->raise(w);
}

} // namespace nefu