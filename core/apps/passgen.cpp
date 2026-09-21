// nefuOS Password Generator
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

struct PassGenState {
    int w, h;
    String password;
    int length;
    uint8_t last_buttons;
    Button btns[3];
};

static const char* CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*";

static String generate_password(int len) {
    String pw;
    uint32_t seed = (uint32_t)platform_tick_ms();
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        int idx = (seed >> 16) % 72;
        pw += CHARS[idx];
    }
    return pw;
}

static void passgen_paint(Window* win) {
    PassGenState* st = (PassGenState*)win->userdata;
    Surface& cs = win->back;
    cs.fill(0x00F5F5F0);

    gfx::text(cs, 10, 8, "Password Generator", color::TEXT, 0x00F5F5F0);

    char buf[64];
    ksprintf(buf, sizeof(buf), "Length: %d", st->length);
    gfx::text(cs, 10, 40, buf, color::TEXT, 0x00F5F5F0);

    // Show password box
    gfx::fillrect(cs, 10, 70, st->w - 20, 40, 0x00FFFFFF);
    gfx::rect(cs, 10, 70, st->w - 20, 40, color::BORDER);

    if (!st->password.empty()) {
        gfx::text(cs, 16, 82, st->password.c_str(), 0x002266CC, 0x00FFFFFF);
    } else {
        gfx::text(cs, 16, 82, "Click Generate...", 0x00999999, 0x00FFFFFF);
    }

    // Buttons
    int btn_w = (st->w - 30) / 2;
    st->btns[0].x = 10; st->btns[0].y = 130; st->btns[0].w = btn_w; st->btns[0].h = 28; st->btns[0].label = "< Len"; st->btns[0].id = 0;
    st->btns[1].x = 20 + btn_w; st->btns[1].y = 130; st->btns[1].w = btn_w; st->btns[1].h = 28; st->btns[1].label = "Len >"; st->btns[1].id = 1;
    st->btns[2].x = 10; st->btns[2].y = 170; st->btns[2].w = st->w - 20; st->btns[2].h = 32; st->btns[2].label = "Generate"; st->btns[2].id = 2;

    for (int i = 0; i < 3; i++) {
        ui::draw_button(cs, st->btns[i]);
    }

    gfx::text(cs, 10, st->h - 24, "nefuOS Password Generator", 0x00888888, 0x00F5F5F0);
}

static void passgen_mouse(Window* w, int mx, int my, uint8_t buttons) {
    PassGenState* st = (PassGenState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;

    for (int i = 0; i < 3; i++) {
        if (ui::button_event(st->btns[i], mx, my, buttons, pressed, released)) {
            if (st->btns[i].id == 0) {
                if (st->length > 6) st->length -= 2;
            } else if (st->btns[i].id == 1) {
                if (st->length < 32) st->length += 2;
            } else if (st->btns[i].id == 2) {
                st->password = generate_password(st->length);
            }
        }
    }
    st->last_buttons = buttons;
}

void passgen_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Password Generator", x, y, 280, 240);
    if (!w) return;
    PassGenState* st = new PassGenState();
    st->w = w->content_w;
    st->h = w->content_h;
    st->length = 12;
    st->password = "";
    st->last_buttons = 0;
    w->userdata = st;
    w->on_paint = passgen_paint;
    w->on_mouse = passgen_mouse;
    g_wm->raise(w);
}

} // namespace nefu
