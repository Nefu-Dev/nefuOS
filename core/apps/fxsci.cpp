// nefuOS fx scientific calculator (fx-991 style).
// Live expression preview + DEG/RAD mode, built on the fx fixed-point library
// via the shared fxexpr parser. Runs on the host and the bare-metal kernel.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sys/settings.h"
#include "../lib/fxexpr.h"

namespace nefu {

namespace {

using namespace fxexpr;
using fx::fix;
using fx::itofix;
using fx::FX_ONE;

const int WIN_W = 440, WIN_H = 600;
const int MAX_BUF = 128;
const int MODE_RAD = 0, MODE_DEG = 1;

// display area
const int DISP_Y = 8, DISP_H = 118;
// button grid: 5 columns x 8 rows
const int COLS = 5, ROWS = 8;
const int BTN_X0 = 8, BTN_Y0 = DISP_Y + DISP_H + 8;
const int BTN_W = 80, BTN_H = 42, BTN_GAP = 4;

struct SciState {
    char buf[MAX_BUF];
    int  cursor;
    int  mode;            // MODE_RAD / MODE_DEG
    fix  last_result;     // Ans
    bool has_result;
    int  mouse_x, mouse_y;
};

static SciState* sc_of(Window* w) { return (SciState*)w->userdata; }

enum BtnKind { BK_NUM, BK_OP, BK_FUNC, BK_AC, BK_EQ };

struct SciBtn { const char* label; const char* ins; BtnKind kind; };

static const SciBtn s_btns[ROWS][COLS] = {
    // row 0
    {{"sin", "sin(", BK_FUNC}, {"cos", "cos(", BK_FUNC}, {"tan", "tan(", BK_FUNC},
     {"ln", "ln(", BK_FUNC}, {"log", "log(", BK_FUNC}},
    // row 1
    {{"asin", "asin(", BK_FUNC}, {"acos", "acos(", BK_FUNC}, {"atan", "atan(", BK_FUNC},
     {"e^x", "exp(", BK_FUNC}, {"10^x", "10^(", BK_FUNC}},
    // row 2
    {{"sqrt", "sqrt(", BK_FUNC}, {"x^2", "^2", BK_FUNC}, {"x^y", "^", BK_OP},
     {"(", "(", BK_FUNC}, {")", ")", BK_FUNC}},
    // row 3
    {{"pi", "pi", BK_FUNC}, {"e", "e", BK_FUNC}, {"abs", "abs(", BK_FUNC},
     {"1/x", "\x11", BK_FUNC}, {"%", "\x12", BK_FUNC}},
    // row 4
    {{"7", "7", BK_NUM}, {"8", "8", BK_NUM}, {"9", "9", BK_NUM},
     {"/", "/", BK_OP}, {"AC", "\x10", BK_AC}},
    // row 5
    {{"4", "4", BK_NUM}, {"5", "5", BK_NUM}, {"6", "6", BK_NUM},
     {"*", "*", BK_OP}, {"DEL", "\x13", BK_AC}},
    // row 6
    {{"1", "1", BK_NUM}, {"2", "2", BK_NUM}, {"3", "3", BK_NUM},
     {"-", "-", BK_OP}, {"+/-", "\x14", BK_OP}},
    // row 7
    {{"0", "0", BK_NUM}, {".", ".", BK_NUM}, {"Ans", "\x15", BK_FUNC},
     {"+", "+", BK_OP}, {"=", "\x16", BK_EQ}},
};

static void insert_text(SciState* st, const char* t) {
    int len = (int)strlen(st->buf);
    int tl = (int)strlen(t);
    if (len + tl >= MAX_BUF - 1) return;
    for (int i = len; i >= st->cursor; i--) st->buf[i + tl] = st->buf[i];
    for (int i = 0; i < tl; i++) st->buf[st->cursor + i] = t[i];
    st->cursor += tl;
}

static void backspace(SciState* st) {
    if (st->cursor <= 0) return;
    int len = (int)strlen(st->buf);
    for (int i = st->cursor - 1; i < len; i++) st->buf[i] = st->buf[i + 1];
    st->cursor--;
}

static void delete_after(SciState* st) {
    int len = (int)strlen(st->buf);
    if (st->cursor >= len) return;
    for (int i = st->cursor; i < len; i++) st->buf[i] = st->buf[i + 1];
}

// wrap the whole current expression: helper for 1/x and +/-
static void wrap_expr(SciState* st, const char* head, const char* tail) {
    char tmp[MAX_BUF];
    strcpy(tmp, st->buf);
    st->buf[0] = 0;
    st->cursor = 0;
    insert_text(st, head);
    insert_text(st, tmp);
    insert_text(st, tail);
}

static void press(SciState* st, const SciBtn& b) {
    char c = b.ins[0];
    if (b.kind == BK_EQ) {
        // compute, store Ans, keep expression
        fix v = 0;
        bool derr = false;
        if (st->buf[0] != 0 && fxexpr_eval_no_x(st->buf, &v, &derr, st->mode) && !derr) {
            st->last_result = v;
            st->has_result = true;
        }
        return;
    }
    if (c == '\x10') { st->buf[0] = 0; st->cursor = 0; return; }        // AC
    if (c == '\x13') { backspace(st); return; }                          // DEL
    if (c == '\x14') {                                                   // +/-
        if (st->buf[0] == 0) { insert_text(st, "-"); return; }
        if (st->buf[0] == '-') {
            // strip leading '-'
            for (int i = 0; i < (int)strlen(st->buf); i++) st->buf[i] = st->buf[i + 1];
            if (st->cursor > 0) st->cursor--;
        } else {
            wrap_expr(st, "-(", ")");
        }
        return;
    }
    if (c == '\x11') { wrap_expr(st, "1/(", ")"); return; }              // 1/x
    if (c == '\x12') {                                                    // %
        int len = (int)strlen(st->buf);
        if (len == 0) { insert_text(st, "0/100"); return; }
        insert_text(st, "/100");
        return;
    }
    if (c == '\x15') {                                                    // Ans
        if (st->has_result) {
            char a[40];
            fix_to_str(a, sizeof(a), st->last_result, 0, 6);
            insert_text(st, a);
        } else {
            insert_text(st, "0");
        }
        return;
    }
    insert_text(st, b.ins);
}

// ---- display helpers ----
static void paint_display(Surface& s, SciState* st, int cw) {
    // panel
    gfx::fillrect(s, 8, DISP_Y, cw - 16, DISP_H, 0x001B2430);
    gfx::rect(s, 8, DISP_Y, cw - 16, DISP_H, 0x000D1218);
    // mode indicator (clickable)
    const char* mode_str = st->mode == MODE_DEG ? "DEG" : "RAD";
    gfx::text(s, cw - 16 - 3 * 8, DISP_Y + 4, mode_str, 0x00E8C15A, 0x001B2430);
    gfx::text(s, 12, DISP_Y + 4, T("fx 科学计算器", "fx Calculator"), 0x00889AB5, 0x001B2430);

    // expression line (right aligned, showing tail + caret)
    int len = (int)strlen(st->buf);
    int max_chars = (cw - 32) / 8;
    int show_from = len - max_chars;
    if (show_from < 0) show_from = 0;
    char line[MAX_BUF + 2];
    int n = 0;
    for (int i = show_from; i < len && n < MAX_BUF; i++) line[n++] = st->buf[i];
    // insert caret marker
    int caret_local = st->cursor - show_from;
    if (caret_local < 0) caret_local = 0;
    if (caret_local > n) caret_local = n;
    char pre[MAX_BUF], post[MAX_BUF];
    pre[0] = 0; post[0] = 0;
    if (caret_local > 0) { strncpy(pre, line, caret_local); pre[caret_local] = 0; }
    if (caret_local < n) strcpy(post, line + caret_local);
    int line_x = cw - 16 - gfx::text_width(line);
    if (line_x < 12) line_x = 12;
    gfx::text(s, line_x, DISP_Y + 26, pre, 0x00E6EDF5, 0x001B2430);
    gfx::fillrect(s, line_x + gfx::text_width(pre), DISP_Y + 26, 6, 16, 0x0066CCFF);
    gfx::text(s, line_x + gfx::text_width(pre) + 8, DISP_Y + 26, post, 0x00E6EDF5, 0x001B2430);

    // result line
    char res[96];
    if (st->buf[0] == 0) {
        strcpy(res, "");
    } else {
        int perr = ERR_NONE, epos = 0;
        if (!fxexpr_parse(st->buf, &perr, &epos)) {
            strcpy(res, T("语法错误", "Syntax Error"));
        } else {
            fix v = 0;
            bool derr = false;
            if (fxexpr_eval_no_x(st->buf, &v, &derr, st->mode)) {
                fix_to_str(res, sizeof(res), v, 0, 6);
            } else {
                strcpy(res, T("数学错误", "Math Error"));
            }
        }
    }
    int rw = gfx::text_width(res);
    int rx = cw - 16 - rw;
    if (rx < 12) rx = 12;
    gfx::text(s, rx, DISP_Y + 64, res, res[0] == 0 ? 0x004B5A70 : 0x00FFFFFF, 0x001B2430);
    if (st->has_result) {
        char ans[48];
        strcpy(ans, "Ans=");
        char a[40];
        fix_to_str(a, sizeof(a), st->last_result, 0, 6);
        strcat(ans, a);
        gfx::text(s, 12, DISP_Y + 92, ans, 0x007FA0C9, 0x001B2430);
    }
}

// ---- paint ----
static void paint(Window* w) {
    Surface& s = w->back;
    SciState* st = sc_of(w);
    int cw = w->content_w, ch = w->content_h;

    s.fill(0x00E3E1DA);

    paint_display(s, st, cw);

    // buttons
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const SciBtn& b = s_btns[r][c];
            int x = BTN_X0 + c * (BTN_W + BTN_GAP);
            int y = BTN_Y0 + r * (BTN_H + BTN_GAP);
            uint32_t bg;
            switch (b.kind) {
            case BK_NUM:  bg = 0x002C3E50; break;
            case BK_OP:   bg = 0x00E67E22; break;
            case BK_FUNC: bg = 0x002F6FB6; break;
            case BK_AC:   bg = 0x00C0392B; break;
            case BK_EQ:   bg = 0x0027AE60; break;
            default:      bg = 0x002C3E50; break;
            }
            gfx::fillrect(s, x, y, BTN_W, BTN_H, bg);
            gfx::rect(s, x, y, BTN_W, BTN_H, 0x00151515);
            const char* label = b.label;
            int tw = gfx::text_width(label);
            int lx = x + (BTN_W - tw) / 2;
            int ly = y + (BTN_H - 16) / 2;
            if (tw > BTN_W - 8) { lx = x + 4; }
            gfx::text(s, lx, ly, label, 0x00FFFFFF, bg);
        }
    }
    // footer hint
    gfx::text(s, 10, ch - 18, T("点击 DEG/RAD 切换角度模式   Enter=计算  Esc=关闭",
                                 "click DEG/RAD to switch mode   Enter=eval  Esc=close"),
              0x00808080, 0x00E3E1DA);
}

static void sc_paint(Window* w) { paint(w); }

// ---- input ----
static void sc_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    SciState* st = sc_of(w);
    if (!st) return;

    if (e->keycode == KEY_BACKSPACE) { backspace(st); return; }
    if (e->keycode == KEY_DEL)       { delete_after(st); return; }
    if (e->keycode == KEY_LEFT)      { if (st->cursor > 0) st->cursor--; return; }
    if (e->keycode == KEY_RIGHT)     { int l = (int)strlen(st->buf); if (st->cursor < l) st->cursor++; return; }
    if (e->keycode == KEY_HOME)      { st->cursor = 0; return; }
    if (e->keycode == KEY_END)       { st->cursor = (int)strlen(st->buf); return; }
    if (e->keycode == KEY_ENTER) {
        fix v = 0;
        bool derr = false;
        if (st->buf[0] != 0 && fxexpr_eval_no_x(st->buf, &v, &derr, st->mode) && !derr) {
            st->last_result = v;
            st->has_result = true;
        }
        return;
    }
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }

    char c = e->ascii;
    if (c >= 32 && c < 127) {
        char ins[2] = {c, 0};
        insert_text(st, ins);
    }
}

static void sc_mouse(Window* w, int mx, int my, uint8_t buttons) {
    SciState* st = sc_of(w);
    if (!st) return;
    st->mouse_x = mx;
    st->mouse_y = my;
    if (!(buttons & 1)) return;

    // mode toggle on the mode label
    int cw = w->content_w;
    if (my >= DISP_Y && my < DISP_Y + 24 && mx >= cw - 16 - 3 * 8 - 4 && mx < cw - 8) {
        st->mode = st->mode == MODE_DEG ? MODE_RAD : MODE_DEG;
        return;
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int x = BTN_X0 + c * (BTN_W + BTN_GAP);
            int y = BTN_Y0 + r * (BTN_H + BTN_GAP);
            if (mx >= x && mx < x + BTN_W && my >= y && my < y + BTN_H) {
                press(st, s_btns[r][c]);
                return;
            }
        }
    }
}

static void sc_close(Window* w) {
    if (w->userdata) delete (SciState*)w->userdata;
    w->userdata = 0;
}

} // namespace

void fxsci_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("科学计算器", "Scientific Calculator"), x, y, WIN_W, WIN_H);
    if (!w) return;
    SciState* st = new SciState();
    st->buf[0] = 0;
    st->cursor = 0;
    st->mode = MODE_RAD;
    st->last_result = itofix(0);
    st->has_result = false;
    st->mouse_x = -1;
    st->mouse_y = -1;
    w->userdata = st;
    w->on_paint = sc_paint;
    w->on_key = sc_key;
    w->on_mouse = sc_mouse;
    w->on_close = sc_close;
    g_wm->raise(w);
}

} // namespace nefu
