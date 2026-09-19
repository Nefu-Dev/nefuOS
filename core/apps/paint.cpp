// nefuOS paint - WM window (surface back + direct blit), palette, clear, save to VFS
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"

namespace nefu {

struct PaintLvState {
    int w, h;
    uint32_t color;
    bool drawing;
    int last_x, last_y;
    String status;
};

static const uint32_t PALETTE[6] = {
    0x00000000, 0x00E74C3C, 0x00E67E22, 0x002ECC71, 0x003498DB, 0x008E44AD
};

static void paint_wm_save(PaintLvState* st, Window* w) {
    FSNode* f = g_vfs->resolve("/home/user/Pictures/drawing.ppm");
    if (!f) {
        g_vfs->mkdir("/home/user/Pictures");
        f = g_vfs->create_file("/home/user/Pictures/drawing.ppm");
    }
    if (!f) return;
    Surface& s = w->back;
    int W = st->w, H = st->h;
    char head[64];
    int hn = ksprintf(head, sizeof(head), "P6\n%d %d\n255\n", W, H);
    uint32_t sz = (uint32_t)hn + (uint32_t)W * (uint32_t)H * 3;
    uint8_t* ob = (uint8_t*)kalloc(sz);
    if (!ob) return;
    memcpy(ob, head, (size_t)hn);
    uint8_t* p = ob + hn;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint32_t c = s.getpx(8 + x, 58 + y);
            *p++ = (uint8_t)((c >> 16) & 0xFF);
            *p++ = (uint8_t)((c >> 8) & 0xFF);
            *p++ = (uint8_t)(c & 0xFF);
        }
    }
    g_vfs->write_file(f, ob, sz);
    kfree(ob);
    st->status = "/home/user/Pictures/drawing.ppm saved";
}

static void paint_wm_paint(Window* w) {
    PaintLvState* st = (PaintLvState*)w->userdata;
    Surface& s = w->back;
    s.fill(0xFFECEBE6);
    // toolbar: 6 color swatches + Clear + Save
    for (int i = 0; i < 8; i++) {
        int bx = 8 + i * 40, by = 6;
        uint32_t tcol = (i < 6) ? PALETTE[i] : (i == 6 ? 0xCC3333u : 0x3D4B66u);
        gfx::fillrect(s, bx, by, 36, 26, tcol);
        gfx::rect(s, bx, by, 36, 26, 0x00506070);
        if (i >= 6) gfx::text(s, bx + 13, by + 6, (i == 6) ? "C" : "S", color::WHITE, tcol);
    }
    // status line
    gfx::text(s, 8, 36, st->status.c_str(), 0x00667788, 0xFFECEBE6);
    // canvas frame (content preserved between repaints)
    gfx::rect(s, 8, 56, st->w, st->h, 0x00506070);
}

static void paint_wm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    PaintLvState* st = (PaintLvState*)w->userdata;
    bool pressed = (buttons != 0);
    // toolbar hit test
    if (my >= 6 && my < 32) {
        for (int i = 0; i < 8; i++) {
            int bx = 8 + i * 40;
            if (mx >= bx && mx < bx + 36) {
                if (pressed) {
                    if (i == 6) {
                        w->back.fill(0xFFFFFFFF);
                        st->status = "cleared";
                    } else if (i == 7) {
                        paint_wm_save(st, w);
                    } else {
                        st->color = PALETTE[i];
                    }
                }
                return;
            }
        }
    }
    // drawing area: offset (8,58)
    int cx = mx - 8, cy = my - 58;
    if (cx < 0 || cy < 0 || cx >= st->w || cy >= st->h) return;
    Surface& s = w->back;
    if (pressed && !st->drawing) {
        st->drawing = true;
        st->last_x = cx;
        st->last_y = cy;
        gfx::fillcircle(s, cx, cy, 2, st->color);
    } else if (pressed && st->drawing) {
        if (st->last_x >= 0 && st->last_y >= 0) gfx::line(s, st->last_x, st->last_y, cx, cy, st->color);
        st->last_x = cx;
        st->last_y = cy;
    } else if (!pressed && st->drawing) {
        st->drawing = false;
    }
}

void paint_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Paint", x, y, 500, 440);
    if (!w) return;
    PaintLvState* st = new PaintLvState();
    st->w = w->content_w - 16;
    st->h = w->content_h - 72;
    if (st->w < 64) st->w = 64;
    if (st->h < 64) st->h = 64;
    st->color = 0x00000000;
    st->drawing = false;
    st->last_x = -1;
    st->last_y = -1;
    st->status = "draw here";
    w->userdata = st;
    w->on_paint = paint_wm_paint;
    w->on_mouse = paint_wm_mouse;
    g_wm->raise(w);
}
} // namespace nefu
