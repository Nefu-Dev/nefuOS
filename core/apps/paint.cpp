// nefuOS 画板：鼠标绘画、调色板、清空、保存到 VFS
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

struct PaintState {
    uint32_t color;
    bool drawing;
    int last_x, last_y;
    Window* win;
    Button btns[8];
    Button* cur;
    uint8_t last_buttons;
    String saved_as;
};

static const uint32_t PALETTE[8] = {
    0x00000000, 0x00E74C3C, 0x00E67E22, 0x00F1C40F,
    0x002ECC71, 0x003498DB, 0x008E44AD, 0x00FFFFFF
};

static void paint_save(PaintState* st) {
    // 保存画布为 /home/user/Pictures/drawing.ppm（P6 格式，通用）
    FSNode* f = g_vfs->resolve("/home/user/Pictures/drawing.ppm");
    if (!f) {
        g_vfs->mkdir("/home/user/Pictures");
        f = g_vfs->create_file("/home/user/Pictures/drawing.ppm");
    }
    if (!f) return;
    Surface& s = st->win->back;
    int W = s.width, H = s.height;
    // 组装 PPM：头部 + RGB 数据（先算大小）
    char head[64];
    int hn = ksprintf(head, sizeof(head), "P6\n%d %d\n255\n", W, H);
    uint32_t sz = (uint32_t)hn + (uint32_t)W * (uint32_t)H * 3;
    uint8_t* buf = (uint8_t*)kalloc(sz);
    if (!buf) return;
    memcpy(buf, head, (size_t)hn);
    uint8_t* p = buf + hn;
    for (int y = 0; y < H; y++) {
        const uint32_t* row = (const uint32_t*)(s.addr + (size_t)y * (size_t)s.pitch);
        for (int x = 0; x < W; x++) {
            uint32_t c = row[x];
            *p++ = (uint8_t)((c >> 16) & 0xFF);
            *p++ = (uint8_t)((c >> 8) & 0xFF);
            *p++ = (uint8_t)(c & 0xFF);
        }
    }
    g_vfs->write_file(f, buf, sz);
    kfree(buf);
    st->saved_as = "/home/user/Pictures/drawing.ppm";
}

static void paint_click(void* ud) {
    PaintState* st = (PaintState*)ud;
    if (!st->cur) return;
    const char* lab = st->cur->label;
    if (strcmp(lab, "C") == 0) {
        st->win->back.fill(color::WHITE);
        st->saved_as.clear();
    } else if (strcmp(lab, "S") == 0) {
        paint_save(st);
    } else {
        // 色板按钮 label = 序号
        int idx = lab[0] - '0';
        if (idx >= 0 && idx < 8) st->color = PALETTE[idx];
    }
}

static void paint_paint(Window* w) {
    PaintState* st = (PaintState*)w->userdata;
    Surface& s = w->back;
    for (int i = 0; i < 8; i++) ui::draw_button(s, st->btns[i]);
    gfx::fillrect(s, s.width - 34, 6, 28, 22, st->color);
    gfx::rect(s, s.width - 34, 6, 28, 22, color::BORDER);
    if (!st->saved_as.empty()) {
        gfx::text(s, 8, s.height - 18, st->saved_as.c_str(), color::TEXT2, color::WHITE);
    }
}

static void paint_mouse(Window* w, int mx, int my, uint8_t buttons) {
    PaintState* st = (PaintState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    for (int i = 0; i < 8; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;
    // 画布区（工具栏下方）
    if (my < 34) return;
    int cx = mx, cy = my - 34;
    Surface& s = w->back;
    if (pressed) {
        st->drawing = true;
        st->last_x = cx;
        st->last_y = cy;
        gfx::fillcircle(s, cx, cy, 2, st->color); // 单点也画点
        return;
    }
    if (!(buttons & 1)) { st->drawing = false; return; }
    if (st->drawing) {
        if (st->last_x >= 0 && st->last_y >= 0) {
            gfx::line(s, st->last_x, st->last_y, cx, cy, st->color);
        }
        st->last_x = cx; st->last_y = cy;
    }
}

static void paint_close(Window* w) {
    if (w->userdata) delete (PaintState*)w->userdata;
    w->userdata = 0;
}

void paint_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Paint", x, y, 480, 380);
    if (!w) return;
    PaintState* st = new PaintState();
    st->win = w;
    st->color = 0x00000000;
    st->drawing = false;
    st->last_x = -1; st->last_y = -1;
    st->last_buttons = 0;
    st->cur = 0;
    const char* labels[8] = { "0", "1", "2", "3", "4", "5", "6", "7" };
    for (int i = 0; i < 8; i++) {
        Button& b = st->btns[i];
        b.x = 8 + i * 34;
        b.y = 6;
        b.w = 30;
        b.h = 22;
        b.label = labels[i];
        b.pressed = false;
        b.on_click = paint_click;
        b.ud = st;
    }
    // 清空 + 保存按钮
    Button& cb = st->btns[6];
    cb.x = 8 + 6 * 34; cb.label = "C";
    Button& sb = st->btns[7];
    sb.x = 8 + 7 * 34; sb.label = "S";
    w->userdata = st;
    w->on_paint = paint_paint;
    w->on_mouse = paint_mouse;
    w->on_close = paint_close;
    w->back.fill(color::WHITE);
}

} // namespace nefu
