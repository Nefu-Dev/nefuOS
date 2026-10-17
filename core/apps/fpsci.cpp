// nefuOS floating-point scientific calculator (FP Calc).
// Evaluates with single-precision float via the soft-float library
// (core/lib/softfloat.h + core/lib/sfexpr.h), so it runs on the host AND
// the bare-metal kernel (GCC soft-float). Unlike the fixed-point fx calc:
//   - range ±3.4e38, scientific notation display (auto for 1e7+ / 1e-6-)
//   - ~7 significant digits
//   - DEG/RAD, Ans memory, live expression preview
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sys/settings.h"
#include "../lib/sfexpr.h"

namespace nefu {

namespace {

using namespace sfexpr;
using namespace sf;

const int WIN_W = 460, WIN_H = 620;
const int MAX_BUF = 128;
const int MODE_RAD = 0, MODE_DEG = 1;

const int DISP_Y = 8, DISP_H = 122;
const int COLS = 5, ROWS = 8;
const int BTN_X0 = 8, BTN_Y0 = DISP_Y + DISP_H + 8;
const int BTN_W = 84, BTN_H = 42, BTN_GAP = 4;

struct FpState {
    char buf[MAX_BUF];
    int  cursor;
    int  mode;
    float last_result;
    bool has_result;
};

static FpState* fp_of(Window* w) { return (FpState*)w->userdata; }

enum BtnKind { BK_NUM, BK_OP, BK_FUNC, BK_AC, BK_EQ };

struct FpBtn { const char* label; const char* ins; BtnKind kind; };

static const FpBtn s_btns[ROWS][COLS] = {
    {{"sin", "sin(", BK_FUNC}, {"cos", "cos(", BK_FUNC}, {"tan", "tan(", BK_FUNC},
     {"ln", "ln(", BK_FUNC}, {"log", "log(", BK_FUNC}},
    {{"asin", "asin(", BK_FUNC}, {"acos", "acos(", BK_FUNC}, {"atan", "atan(", BK_FUNC},
     {"e^x", "exp(", BK_FUNC}, {"10^x", "10^(", BK_FUNC}},
    {{"sqrt", "sqrt(", BK_FUNC}, {"x^2", "^2", BK_FUNC}, {"x^y", "^", BK_OP},
     {"(", "(", BK_FUNC}, {")", ")", BK_FUNC}},
    {{"pi", "pi", BK_FUNC}, {"e", "e", BK_FUNC}, {"abs", "abs(", BK_FUNC},
     {"1/x", "\x11", BK_FUNC}, {"%", "\x12", BK_FUNC}},
    {{"7", "7", BK_NUM}, {"8", "8", BK_NUM}, {"9", "9", BK_NUM},
     {"/", "/", BK_OP}, {"AC", "\x10", BK_AC}},
    {{"4", "4", BK_NUM}, {"5", "5", BK_NUM}, {"6", "6", BK_NUM},
     {"*", "*", BK_OP}, {"DEL", "\x13", BK_AC}},
    {{"1", "1", BK_NUM}, {"2", "2", BK_NUM}, {"3", "3", BK_NUM},
     {"-", "-", BK_OP}, {"+/-", "\x14", BK_OP}},
    {{"0", "0", BK_NUM}, {".", ".", BK_NUM}, {"Ans", "\x15", BK_FUNC},
     {"+", "+", BK_OP}, {"=", "\x16", BK_EQ}},
};

static void insert_text(FpState* st, const char* t) {
    int len = (int)strlen(st->buf);
    int tl = (int)strlen(t);
    if (len + tl >= MAX_BUF - 1) return;
    for (int i = len; i >= st->cursor; i--) st->buf[i + tl] = st->buf[i];
    for (int i = 0; i < tl; i++) st->buf[st->cursor + i] = t[i];
    st->cursor += tl;
}

static void backspace(FpState* st) {
    if (st->cursor <= 0) return;
    int len = (int)strlen(st->buf);
    for (int i = st->cursor - 1; i < len; i++) st->buf[i] = st->buf[i + 1];
    st->cursor--;
}

static void delete_after(FpState* st) {
    int len = (int)strlen(st->buf);
    if (st->cursor >= len) return;
    for (int i = st->cursor; i < len; i++) st->buf[i] = st->buf[i + 1];
}

static void wrap_expr(FpState* st, const char* head, const char* tail) {
    char tmp[MAX_BUF];
    strcpy(tmp, st->buf);
    st->buf[0] = 0;
    st->cursor = 0;
    insert_text(st, head);
    insert_text(st, tmp);
    insert_text(st, tail);
}

static void compute(FpState* st) {
    if (st->buf[0] == 0) return;
    float v = 0.0f;
    bool derr = false;
    if (sfexpr_eval_no_x(st->buf, &v, &derr, st->mode) && !derr) {
        st->last_result = v;
        st->has_result = true;
    }
}

static void press(FpState* st, const FpBtn& b) {
    char c = b.ins[0];
    if (b.kind == BK_EQ) { compute(st); return; }
    if (c == '\x10') { st->buf[0] = 0; st->cursor = 0; return; }
    if (c == '\x13') { backspace(st); return; }
    if (c == '\x14') {
        if (st->buf[0] == 0) { insert_text(st, "-"); return; }
        if (st->buf[0] == '-') {
            for (int i = 0; i < (int)strlen(st->buf); i++) st->buf[i] = st->buf[i + 1];
            if (st->cursor > 0) st->cursor--;
        } else {
            wrap_expr(st, "-(", ")");
        }
        return;
    }
    if (c == '\x11') { wrap_expr(st, "1/(", ")"); return; }
    if (c == '\x12') {
        if (strlen(st->buf) == 0) { insert_text(st, "0/100"); return; }
        insert_text(st, "/100");
        return;
    }
    if (c == '\x15') {
        if (st->has_result) {
            char a[40];
            sf_to_str(a, sizeof(a), st->last_result, 7);
            insert_text(st, a);
        } else {
            insert_text(st, "0");
        }
        return;
    }
    insert_text(st, b.ins);
}

static void paint_display(Surface& s, FpState* st, int cw) {
    gfx::fillrect(s, 8, DISP_Y, cw - 16, DISP_H, 0x001A242E);
    gfx::rect(s, 8, DISP_Y, cw - 16, DISP_H, 0x000D1218);
    const char* mode_str = st->mode == MODE_DEG ? "DEG" : "RAD";
    gfx::text(s, cw - 16 - 3 * 8, DISP_Y + 4, mode_str, 0x00E8C15A, 0x001A242E);
    gfx::text(s, 12, DISP_Y + 4, T("FP 浮点计算器", "FP Calculator"), 0x0088A5C0, 0x001A242E);
    gfx::text(s, 12, DISP_Y + 22, "float ±3.4e38 · 7 digits", 0x005C7490, 0x001A242E);

    // expression line with caret
    int len = (int)strlen(st->buf);
    int max_chars = (cw - 32) / 8;
    int show_from = len - max_chars;
    if (show_from < 0) show_from = 0;
    char line[MAX_BUF + 2];
    int n = 0;
    for (int i = show_from; i < len && n < MAX_BUF; i++) line[n++] = st->buf[i];
    int caret_local = st->cursor - show_from;
    if (caret_local < 0) caret_local = 0;
    if (caret_local > n) caret_local = n;
    char pre[MAX_BUF], post[MAX_BUF];
    pre[0] = 0; post[0] = 0;
    if (caret_local > 0) { strncpy(pre, line, caret_local); pre[caret_local] = 0; }
    if (caret_local < n) strcpy(post, line + caret_local);
    int line_x = cw - 16 - gfx::text_width(line);
    if (line_x < 12) line_x = 12;
    gfx::text(s, line_x, DISP_Y + 42, pre, 0x00E6EDF5, 0x001A242E);
    gfx::fillrect(s, line_x + gfx::text_width(pre), DISP_Y + 42, 6, 16, 0x0066CCFF);
    gfx::text(s, line_x + gfx::text_width(pre) + 8, DISP_Y + 42, post, 0x00E6EDF5, 0x001A242E);

    // result line
    char res[96];
    if (st->buf[0] == 0) {
        res[0] = 0;
    } else {
        int perr = ERR_NONE, epos = 0;
        if (!sfexpr_parse(st->buf, &perr, &epos)) {
            strcpy(res, T("语法错误", "Syntax Error"));
        } else {
            float v = 0.0f;
            bool derr = false;
            if (sfexpr_eval_no_x(st->buf, &v, &derr, st->mode)) {
                sf_to_str(res, sizeof(res), v, 7);
            } else {
                strcpy(res, T("数学错误", "Math Error"));
            }
        }
    }
    int rw = gfx::text_width(res);
    int rx = cw - 16 - rw;
    if (rx < 12) rx = 12;
    gfx::text(s, rx, DISP_Y + 74, res, res[0] == 0 ? 0x004B5A70 : 0x00FFFFFF, 0x001A242E);
    if (st->has_result) {
        char ans[48];
        strcpy(ans, "Ans=");
        char a[40];
        sf_to_str(a, sizeof(a), st->last_result, 7);
        strcat(ans, a);
        gfx::text(s, 12, DISP_Y + 100, ans, 0x007FA0C9, 0x001A242E);
    }
}

static void paint(Window* w) {
    Surface& s = w->back;
    FpState* st = fp_of(w);
    int cw = w->content_w, ch = w->content_h;
    s.fill(0x00E3E1DA);
    paint_display(s, st, cw);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const FpBtn& b = s_btns[r][c];
            int x = BTN_X0 + c * (BTN_W + BTN_GAP);
            int y = BTN_Y0 + r * (BTN_H + BTN_GAP);
            uint32_t bg;
            switch (b.kind) {
            case BK_NUM:  bg = 0x002C3E50; break;
            case BK_OP:   bg = 0x00D35400; break;
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
            if (tw > BTN_W - 8) lx = x + 4;
            gfx::text(s, lx, y + (BTN_H - 16) / 2, label, 0x00FFFFFF, bg);
        }
    }
    gfx::text(s, 10, ch - 18, T("浮点引擎：范围 1e-38~3.4e38，自动科学计数法  Enter=计算  Esc=关闭",
                                 "float engine: 1e-38..3.4e38, auto sci-notation  Enter=eval  Esc=close"),
              0x00808080, 0x00E3E1DA);
}

static void fp_paint(Window* w) { paint(w); }

static void fp_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    FpState* st = fp_of(w);
    if (!st) return;
    if (e->keycode == KEY_BACKSPACE) { backspace(st); return; }
    if (e->keycode == KEY_DEL)       { delete_after(st); return; }
    if (e->keycode == KEY_LEFT)      { if (st->cursor > 0) st->cursor--; return; }
    if (e->keycode == KEY_RIGHT)     { int l = (int)strlen(st->buf); if (st->cursor < l) st->cursor++; return; }
    if (e->keycode == KEY_HOME)      { st->cursor = 0; return; }
    if (e->keycode == KEY_END)       { st->cursor = (int)strlen(st->buf); return; }
    if (e->keycode == KEY_ENTER)     { compute(st); return; }
    if (e->keycode == KEY_ESC)       { g_wm->close_window(w); return; }
    char c = e->ascii;
    if (c >= 32 && c < 127) {
        char ins[2] = {c, 0};
        insert_text(st, ins);
    }
}

static void fp_mouse(Window* w, int mx, int my, uint8_t buttons) {
    FpState* st = fp_of(w);
    if (!st) return;
    if (!(buttons & 1)) return;
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

static void fp_close(Window* w) {
    if (w->userdata) delete (FpState*)w->userdata;
    w->userdata = 0;
}

} // namespace

void fpsci_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("浮点计算器", "FP Calculator"), x, y, WIN_W, WIN_H);
    if (!w) return;
    FpState* st = new FpState();
    st->buf[0] = 0;
    st->cursor = 0;
    st->mode = MODE_RAD;
    st->last_result = 0.0f;
    st->has_result = false;
    w->userdata = st;
    w->on_paint = fp_paint;
    w->on_key = fp_key;
    w->on_mouse = fp_mouse;
    w->on_close = fp_close;
    g_wm->raise(w);
}

} // namespace nefu
