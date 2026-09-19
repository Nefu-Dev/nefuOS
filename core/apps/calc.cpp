// nefuOS calculator - LVGL GUI (integer arithmetic, no floats)
#include "apps.h"
#include "../gui/lvgl_win.h"

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

struct CalcLvState {
    String expr;
    bool err;
    bool just_eq;
    LvglWin* lw;
    lv_obj_t* disp;
    lv_obj_t* eqdisp;
};

struct CalcKey {
    CalcLvState* st;
    char label[4];
};

static void calc_lv_key(lv_event_t* e) {
    CalcKey* k = (CalcKey*)lv_event_get_user_data(e);
    if (!k || !k->st) return;
    CalcLvState* st = k->st;
    const char* lab = k->label;
    if (strcmp(lab, "C") == 0) {
        st->expr.clear();
        st->err = false;
        st->just_eq = false;
    } else if (strcmp(lab, "=") == 0) {
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
    } else {
        if (st->err) { st->expr.clear(); st->err = false; }
        if (st->just_eq) {
            char c = lab[0];
            if (c >= '0' && c <= '9') st->expr.clear();
            st->just_eq = false;
        }
        if (st->expr.len() < 40) {
            if (st->expr.empty() && (lab[0] == '*' || lab[0] == '/')) return;
            st->expr += lab;
        }
    }
    lv_label_set_text(st->disp, st->err ? "Error" : st->expr.c_str());
    lv_obj_set_style_text_color(st->disp, st->err ? lv_color_hex(0xCC3333) : lv_color_hex(0x15181E), 0);
    if (st->just_eq && !st->err) {
        char buf[32];
        ksprintf(buf, sizeof(buf), "= %s", st->expr.c_str());
        lv_label_set_text(st->eqdisp, buf);
    } else {
        lv_label_set_text(st->eqdisp, "");
    }
}

void calc_launch() {
    static const char* labels[16] = {
        "7", "8", "9", "/",
        "4", "5", "6", "*",
        "1", "2", "3", "-",
        "C", "0", "=", "+"
    };
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Calculator", x, y, 264, 330);
    if (!lw) return;
    CalcLvState* st = new CalcLvState();
    st->lw = lw;
    st->err = false;
    st->just_eq = false;
    lw->userdata = st;

    // display panel (white box, black text)
    lv_obj_t* panel = lv_obj_create(lw->content);
    lv_obj_set_pos(panel, 8, 8);
    lv_obj_set_size(panel, 248, 64);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x9AA5BF), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    st->disp = lv_label_create(panel);
    lv_obj_align(st->disp, LV_ALIGN_TOP_LEFT, 8, 6);
    lv_obj_set_width(st->disp, 232);
    lv_label_set_text(st->disp, "0");
    lv_obj_set_style_text_color(st->disp, lv_color_hex(0x15181E), 0);
    lv_obj_set_style_text_font(st->disp, &lv_font_montserrat_14, 0);
    st->eqdisp = lv_label_create(panel);
    lv_obj_align(st->eqdisp, LV_ALIGN_BOTTOM_LEFT, 8, 0);
    lv_obj_set_style_text_color(st->eqdisp, lv_color_hex(0x3366AA), 0);
    lv_obj_set_style_text_font(st->eqdisp, &lv_font_montserrat_14, 0);

    // key grid: 4x4
    int bw = 54, bh = 42, gap = 6, x0 = 8, y0 = 84;
    for (int i = 0; i < 16; i++) {
        int r = i / 4, c = i % 4;
        CalcKey* k = new CalcKey();
        k->st = st;
        strncpy(k->label, labels[i], 3);
        k->label[3] = 0;
        lv_obj_t* btn = lv_button_create(lw->content);
        lv_obj_set_pos(btn, x0 + c * (bw + gap), y0 + r * (bh + gap));
        lv_obj_set_size(btn, bw, bh);
        lv_obj_set_style_radius(btn, 6, 0);
        char c0 = labels[i][0];
        bool op = !((c0 >= '0' && c0 <= '9') || c0 == 'C');
        lv_obj_set_style_bg_color(btn, op ? lv_color_hex(0x3D4B66) : lv_color_hex(0x55678A), 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x2B3347), LV_STATE_PRESSED);
        lv_obj_add_event_cb(btn, calc_lv_key, LV_EVENT_CLICKED, k);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    }
}

} // namespace nefu