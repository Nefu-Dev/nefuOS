// nefuOS Paint Application
#pragma once

#include "../gfx/gfx.h"
#include "../wm.h"
#include "../klib/klib.h"
#include "../vfs/vfs.h"

namespace nefu {
namespace apps {

#define PAINT_W 640
#define PAINT_H 400

struct PaintState {
    Surface canvas;
    uint32_t color;
    int tool; // 0=brush, 1=eraser, 2=line, 3=rect, 4=circle
    int brush_size;
    int start_x, start_y;
    bool drawing;
    
    PaintState() : color(0x000000), tool(0), brush_size(3), drawing(false) {
        canvas.addr = (uint8_t*)kalloc(PAINT_W * PAINT_H * 4);
        canvas.width = PAINT_W;
        canvas.height = PAINT_H;
        canvas.pitch = PAINT_W * 4;
        canvas.fill(0xFFFFFF);
    }
    
    ~PaintState() {
        kfree(canvas.addr);
    }
    
    void draw_pixel(int x, int y, uint32_t c) {
        if (tool == 1) c = 0xFFFFFF; // eraser
        for (int dy = -brush_size/2; dy <= brush_size/2; dy++) {
            for (int dx = -brush_size/2; dx <= brush_size/2; dx++) {
                int px = x + dx;
                int py = y + dy;
                if (px >= 0 && px < PAINT_W && py >= 0 && py < PAINT_H) {
                    canvas.setpx(px, py, c);
                }
            }
        }
    }
    
    void save(const char* path) {
        FSNode* f = g_vfs->create(path, FS_REGULAR);
        if (!f) return;
        // Save as raw RGBA for now
        g_vfs->write_file(f, canvas.addr, PAINT_W * PAINT_H * 4);
    }
};

static void paint_paint(Window* w) {
    PaintState* st = (PaintState*)w->userdata;
    if (!st) return;
    
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x34495E);
    
    // Canvas
    int canvas_x = 50;
    int canvas_y = 10;
    gfx::blit(s, st->canvas, canvas_x, canvas_y);
    
    // Color palette
    uint32_t colors[] = {
        0x000000, 0xFF0000, 0xFF7F00, 0xFFFF00,
        0x00FF00, 0x0000FF, 0x4B0082, 0x9400D3,
        0xFFFFFF, 0xC0C0C0, 0x808080, 0x800000
    };
    
    for (int i = 0; i < 12; i++) {
        int x = 5;
        int y = 10 + i * 28;
        gfx::fillrect(s, x, y, 36, 24, colors[i]);
        if (st->color == colors[i]) {
            gfx::rect(s, x - 2, y - 2, 40, 28, 0x00FF00);
        }
    }
    
    // Tools
    const char* tools[] = { "B", "E", "L", "R", "C" };
    for (int i = 0; i < 5; i++) {
        int x = 5;
        int y = 350 + i * 30;
        uint32_t c = (st->tool == i) ? 0x3498DB : 0x2C3E50;
        gfx::fillrect(s, x, y, 36, 24, c);
        gfx::text(s, x + 12, y + 6, tools[i], 0xFFFFFF, c);
    }
}

static void paint_mouse(Window* w, int mx, int my, uint8_t buttons) {
    PaintState* st = (PaintState*)w->userdata;
    if (!st) return;
    
    int canvas_x = 50;
    int canvas_y = 10;
    
    // Check palette clicks
    uint32_t colors[] = {
        0x000000, 0xFF0000, 0xFF7F00, 0xFFFF00,
        0x00FF00, 0x0000FF, 0x4B0082, 0x9400D3,
        0xFFFFFF, 0xC0C0C0, 0x808080, 0x800000
    };
    
    for (int i = 0; i < 12; i++) {
        int x = 5;
        int y = 10 + i * 28;
        if (mx >= x && mx < x + 36 && my >= y && my < y + 24 && (buttons & 1)) {
            st->color = colors[i];
            return;
        }
    }
    
    // Check tool clicks
    for (int i = 0; i < 5; i++) {
        int x = 5;
        int y = 350 + i * 30;
        if (mx >= x && mx < x + 36 && my >= y && my < y + 24 && (buttons & 1)) {
            st->tool = i;
            return;
        }
    }
    
    // Canvas drawing
    if (mx >= canvas_x && mx < canvas_x + PAINT_W && my >= canvas_y && my < canvas_y + PAINT_H) {
        int cx = mx - canvas_x;
        int cy = my - canvas_y;
        
        if (buttons & 1) {
            st->draw_pixel(cx, cy, st->color);
        }
    }
}

Window* open_paint() {
    Window* w = g_wm->create_window("Paint", 100, 100, 700, 480);
    if (!w) return 0;
    
    PaintState* st = new PaintState();
    w->userdata = st;
    w->on_paint = paint_paint;
    w->on_mouse = paint_mouse;
    
    return w;
}

} // namespace apps
} // namespace nefu
