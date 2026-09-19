// nefuOS paint - LVGL GUI (canvas drawing, palette, clear, save to VFS)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/gfx.h"

namespace nefu {

struct PaintLvState {
    LvglWin* lw;
    lv_obj_t* canvas;
    uint8_t* buf;
    int w, h;
    uint32_t color;
    bool drawing;
    int last_x, last_y;
    lv_obj_t* status;
};

static const uint32_t PALETTE[6] = {
    0x00000000, 0x00E74C3C, 0x00E67E22, 0x002ECC71, 0x003498DB, 0x008E44AD
};

static int obj_abs_x(lv_obj_t* o) {
    int ax = 0;
    while (o && o != lv_screen_active()) { ax += lv_obj_get_x(o); o = lv_obj_get_parent(o); }
    return ax;
}
static int obj_abs_y(lv_obj_t* o) {
    int ay = 0;
    while (o && o != lv_screen_active()) { ay += lv_obj_get_y(o); o = lv_obj_get_parent(o); }
    return ay;
}

static void paint_lv_save(PaintLvState* st) {
    FSNode* f = g_vfs->resolve("/home/user/Pictures/drawing.ppm");
    if (!f) {
        g_vfs->mkdir("/home/user/Pictures");
        f = g_vfs->create_file("/home/user/Pictures/drawing.ppm");
    }
    if (!f) return;
    int W = st->w, H = st->h;
    char head[64];
    int hn = ksprintf(head, sizeof(head), "P6\n%d %d\n255\n", W, H);
    uint32_t sz = (uint32_t)hn + (uint32_t)W * (uint32_t)H * 3;
    uint8_t* ob = (uint8_t*)kalloc(sz);
    if (!ob) return;
    memcpy(ob, head, (size_t)hn);
    uint8_t* p = ob + hn;
    for (int y = 0; y < H; y++) {
        const uint32_t* row = (const uint32_t*)(st->buf + (size_t)y * (size_t)W * 4);
        for (int x = 0; x < W; x++) {
            uint32_t c = row[x];
            *p++ = (uint8_t)((c >> 16) & 0xFF);
            *p++ = (uint8_t)((c >> 8) & 0xFF);
            *p++ = (uint8_t)(c & 0xFF);
        }
    }
    g_vfs->write_file(f, ob, sz);
    kfree(ob);
    if (st->status) lv_label_set_text(st->status, "/home/user/Pictures/drawing.ppm saved");
}

static void paint_lv_tool(lv_event_t* e) {
    PaintLvState* st = (PaintLvState*)lv_event_get_user_data(e);
    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    if (idx == 6) {
        // clear
        if (st->buf) { memset(st->buf, 0xFF, (size_t)st->w * (size_t)st->h * 4); lv_obj_invalidate(st->canvas); }
        if (st->status) lv_label_set_text(st->status, "cleared");
    } else if (idx == 7) {
        paint_lv_save(st);
    } else if (idx >= 0 && idx < 6) {
        st->color = PALETTE[idx];
    }
}

static void paint_lv_mouse(lv_event_t* e) {
    PaintLvState* st = (PaintLvState*)lv_event_get_user_data(e);
    if (!st->canvas || !st->buf) return;
    lv_indev_t* ind = lv_indev_active();
    if (!ind) return;
    lv_point_t pt;
    lv_indev_get_point(ind, &pt);
    int cx = pt.x - obj_abs_x(st->canvas);
    int cy = pt.y - obj_abs_y(st->canvas);
    if (cx < 0 || cy < 0 || cx >= st->w || cy >= st->h) return;
    Surface s;
    s.addr = st->buf;
    s.width = st->w;
    s.height = st->h;
    s.pitch = st->w * 4;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        st->drawing = true;
        st->last_x = cx;
        st->last_y = cy;
        gfx::fillcircle(s, cx, cy, 2, st->color);
    } else if (code == LV_EVENT_PRESSING) {
        if (st->drawing) {
            if (st->last_x >= 0 && st->last_y >= 0) {
                gfx::line(s, st->last_x, st->last_y, cx, cy, st->color);
            }
            st->last_x = cx;
            st->last_y = cy;
        }
    } else if (code == LV_EVENT_RELEASED) {
        st->drawing = false;
    }
    lv_obj_invalidate(st->canvas);
}

void paint_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Paint", x, y, 480, 408);
    if (!lw) return;
    PaintLvState* st = new PaintLvState();
    st->lw = lw;
    st->color = 0x00000000;
    st->drawing = false;
    st->last_x = -1;
    st->last_y = -1;
    lw->userdata = st;

    // toolbar: 6 color swatches + Clear + Save
    const char* tools[8] = { "0","1","2","3","4","5","C","S" };
    uint32_t tcol[8] = { 0x000000, 0xE74C3C, 0xE67E22, 0x2ECC71, 0x3498DB, 0x8E44AD, 0xCC3333, 0x3D4B66 };
    for (int i = 0; i < 8; i++) {
        lv_obj_t* b = lv_button_create(lw->content);
        lv_obj_set_pos(b, 8 + i * 36, 6);
        lv_obj_set_size(b, 32, 24);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(tcol[i]), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2B3347), LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, paint_lv_tool, LV_EVENT_CLICKED, st);
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
        if (i >= 6) {
            lv_obj_t* lbl = lv_label_create(b);
            lv_label_set_text(lbl, tools[i]);
            lv_obj_center(lbl);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        }
    }

    // status line
    st->status = lv_label_create(lw->content);
    lv_obj_set_pos(st->status, 8, 34);
    lv_obj_set_style_text_color(st->status, lv_color_hex(0x667788), 0);
    lv_label_set_text(st->status, "draw here");

    // drawing canvas
    st->w = 464;
    st->h = 308;
    st->canvas = lv_canvas_create(lw->content);
    lv_obj_set_pos(st->canvas, 8, 56);
    lv_obj_set_size(st->canvas, st->w, st->h);
    int bufsz = lv_canvas_buf_size(st->w, st->h, 32, 4);
    st->buf = new uint8_t[bufsz];
    memset(st->buf, 0xFF, (size_t)bufsz);
    lv_canvas_set_buffer(st->canvas, st->buf, st->w, st->h, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_add_flag(st->canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(st->canvas, paint_lv_mouse, LV_EVENT_PRESSED, st);
    lv_obj_add_event_cb(st->canvas, paint_lv_mouse, LV_EVENT_PRESSING, st);
    lv_obj_add_event_cb(st->canvas, paint_lv_mouse, LV_EVENT_RELEASED, st);
}
} // namespace nefu