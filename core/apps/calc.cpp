// nefuOS 计算器（整数四则运算）
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"

namespace nefu {

struct CalcParser {
    const char* p;
    bool err;
    int parse_expr();
    int parse_term();
    int parse_factor();
};

int CalcParser::parse_expr() {
    int v = parse_term();
    while (!err && (*p == '+' || *p == '-')) {
        char op = *p++;
        int r = parse_term();
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

int CalcParser::parse_term() {
    int v = parse_factor();
    while (!err && (*p == '*' || *p == '/')) {
        char op = *p++;
        int r = parse_factor();
        if (op == '/') { if (r == 0) { err = true; return 0; } v /= r; }
        else v *= r;
    }
    return v;
}

int CalcParser::parse_factor() {
    while (*p == ' ') p++;
    if (*p == '-') { p++; return -parse_factor(); }
    if (*p == '+') { p++; return parse_factor(); }
    if (*p == '(') {
        p++;
        int v = parse_expr();
        while (*p == ' ') p++;
        if (*p == ')') p++; else err = true;
        return v;
    }
    if (*p >= '0' && *p <= '9') {
        int v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
        return v;
    }
    err = true;
    return 0;
}

static bool calc_eval(const char* s, int* out) {
    CalcParser cp;
    cp.p = s;
    cp.err = false;
    int v = cp.parse_expr();
    while (*cp.p == ' ') cp.p++;
    if (cp.err || *cp.p) return false;
    *out = v;
    return true;
}

struct CalcState {
    String expr;
    bool err;
    bool just_eq;
    Window* win;
    Button btns[16];
    Button* cur;
    uint8_t last_buttons;
};

static void calc_click(void* ud) {
    CalcState* st = (CalcState*)ud;
    if (!st->cur) return;
    const char* lab = st->cur->label;
    if (strcmp(lab, "C") == 0) {
        st->expr.clear();
        st->err = false;
        st->just_eq = false;
        return;
    }
    if (strcmp(lab, "=") == 0) {
        int v = 0;
        if (calc_eval(st->expr.c_str(), &v)) {
            char buf[32];
            ksprintf(buf, sizeof(buf), "%d", v);
            st->expr = buf;
            st->err = false;
        } else {
            st->err = true;
        }
        st->just_eq = true;
        return;
    }
    if (st->err) { st->expr.clear(); st->err = false; }
    if (st->just_eq) {
        // 按数字则替换结果，按运算符则继续
        char c = lab[0];
        if (c >= '0' && c <= '9') st->expr.clear();
        st->just_eq = false;
    }
    if (st->expr.len() < 40) {
        if (st->expr.empty() && (lab[0] == '*' || lab[0] == '/')) return;
        st->expr += lab;
    }
}

static void calc_paint(Window* w) {
    CalcState* st = (CalcState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::PANEL);
    // 显示屏
    gfx::fillrect(s, 8, 8, s.width - 16, 64, color::WHITE);
    gfx::rect(s, 8, 8, s.width - 16, 64, color::BORDER);
    gfx::text(s, 14, 14, st->err ? "Error" : st->expr.c_str(), st->err ? color::RED : color::TEXT, color::WHITE);
    if (!st->err && st->just_eq) {
        char buf[32];
        ksprintf(buf, sizeof(buf), "= %s", st->expr.c_str());
        gfx::text(s, 14, 34, buf, color::BLUE, color::WHITE);
    }
    for (int i = 0; i < 16; i++) ui::draw_button(s, st->btns[i]);
}

static void calc_mouse(Window* w, int mx, int my, uint8_t buttons) {
    CalcState* st = (CalcState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    for (int i = 0; i < 16; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;
}

static void calc_close(Window* w) {
    if (w->userdata) delete (CalcState*)w->userdata;
    w->userdata = 0;
}

void calc_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Calculator", x, y, 264, 330);
    if (!w) return;
    CalcState* st = new CalcState();
    st->win = w;
    st->err = false;
    st->just_eq = false;
    st->cur = 0;
    st->last_buttons = 0;
    const char* labels[16] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "C", "0", "=", "+"
    };
    int bw = 54, bh = 42, gap = 6;
    int x0 = 8, y0 = 80;
    for (int i = 0; i < 16; i++) {
        int r = i / 4, c = i % 4;
        Button& b = st->btns[i];
        b.x = x0 + c * (bw + gap);
        b.y = y0 + r * (bh + gap);
        b.w = bw;
        b.h = bh;
        b.label = labels[i];
        b.pressed = false;
        b.on_click = calc_click;
        b.ud = st;
    }
    w->userdata = st;
    w->on_paint = calc_paint;
    w->on_mouse = calc_mouse;
    w->on_close = calc_close;
}

} // namespace nefu
