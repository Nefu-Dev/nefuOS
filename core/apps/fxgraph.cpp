// nefuOS fx function grapher — plot y = f(x) using the fx fixed-point library.
// Features:
//   - expression input (keyboard) with cursor editing
//   - quick function buttons (sin/cos/tan/.../ln/log), symbol buttons (x^2, ^, (), pi, e)
//   - coordinate grid with adaptive (nice) tick steps, axes, tick labels
//   - mouse wheel zoom, arrow-key pan, R reset view, S save curve, C clear history
//   - up to 3 saved curves (different colors) plus the live curve
//   - live mouse crosshair with f(x) value in the status bar
// All math runs through nefu::fx Q16.16 fixed point (bare-metal safe).
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

const int WIN_W = 800, WIN_H = 560;

// layout (all coordinates in window content space)
const int INPUT_Y  = 8;
const int INPUT_H  = 30;
const int BTN1_Y   = 44;
const int BTN2_Y   = 76;
const int BTN_H    = 28;
const int BTN_GAP  = 4;
const int HINT_Y   = 110;
const int HINT_H   = 18;
const int STAT_H   = 20;

const int MAX_EXPR = 96;
const int MAX_SAVED = 3;

struct SavedCurve { char expr[MAX_EXPR]; uint32_t color; };

struct GraphState {
    char expr[MAX_EXPR];
    int  cursor;                       // caret position (chars)
    fix  vxmin, vxmax, vymin, vymax;   // viewport
    SavedCurve saved[MAX_SAVED];
    int  nsaved;
    int  plot_x, plot_y, plot_w, plot_h;  // cached plot rect
    int  mouse_x, mouse_y;             // last mouse pos in content space (-1 = out)
    int  btn_rows[2][10];              // -1..1 hover state (unused, kept for layout)
    bool hover;                        // mouse inside plot area
};

static GraphState* gs_of(Window* w) { return (GraphState*)w->userdata; }

// ---- view helpers ----
static void reset_view(GraphState* st, int pw, int ph) {
    st->vxmin = itofix(-10);
    st->vxmax = itofix(10);
    if (pw > 0 && ph > 0) {
        fix vxr = st->vxmax - st->vxmin;
        fix yr = fx::fx_div(fx::fx_mul(vxr, itofix(ph)), itofix(pw));
        st->vymin = (fix)-(yr >> 1);
        st->vymax = yr >> 1;
    }
}

static void pan_view(GraphState* st, fix dx_frac, fix dy_frac) {
    fix w = st->vxmax - st->vxmin;
    fix h = st->vymax - st->vymin;
    fix dx = fx::fx_mul(w, dx_frac);
    fix dy = fx::fx_mul(h, dy_frac);
    st->vxmin += dx; st->vxmax += dx;
    st->vymin += dy; st->vymax += dy;
}

static void zoom_view(GraphState* st, fix factor) {
    fix cx = (st->vxmin + st->vxmax) >> 1;
    fix cy = (st->vymin + st->vymax) >> 1;
    fix w = fx::fx_mul(st->vxmax - st->vxmin, factor);
    fix h = fx::fx_mul(st->vymax - st->vymin, factor);
    st->vxmin = cx - (w >> 1); st->vxmax = cx + (w >> 1);
    st->vymin = cy - (h >> 1); st->vymax = cy + (h >> 1);
}

// ---- nice tick step: 1/2/5 * 10^k >= target ----
static fix nice_step(fix target) {
    // build 10^k in Q16.16 for k = -4..4
    for (int k = -4; k <= 4; k++) {
        fix p10;
        if (k >= 0) p10 = itofix(1); else p10 = fx::fxf(1, 10000);
        for (int i = 0; i < (k >= 0 ? k : -k); i++) {
            if (k >= 0) p10 = fx::fx_mul(p10, itofix(10));
            else        p10 = fx::fx_div(p10, itofix(10));
        }
        const fix mults[3] = { FX_ONE, itofix(2), itofix(5) };
        for (int m = 0; m < 3; m++) {
            fix s = fx::fx_mul(mults[m], p10);
            if (s >= target) return s;
        }
    }
    return itofix(1);
}

// first multiple of step >= min
static fix ceil_mult(fix min, fix step) {
    fix q = fx::fx_div(min, step);
    fix r = q;  // fix has 16 frac bits; round toward +inf
    // floor division then check
    fix fl = fx::fx_floor(q);
    fix v = fx::fx_mul(fl, step);
    if (v < min) v += step;
    return v;
}

static int y_to_px(const GraphState* st, fix y) {
    fix t = fx::fx_div(fx::fx_mul(y - st->vymin, itofix(st->plot_h)), st->vymax - st->vymin);
    return st->plot_y + st->plot_h - 1 - fx::fixtoi(t);
}

static fix px_to_x(const GraphState* st, int px) {
    return st->vxmin + fx::fx_div(fx::fx_mul(st->vxmax - st->vxmin, itofix(px)), itofix(st->plot_w));
}

// ---- button model ----
struct Btn { const char* label; const char* insert; bool funct; };

static const Btn s_btn1[10] = {
    {"sin", "sin(", true}, {"cos", "cos(", true}, {"tan", "tan(", true},
    {"asin", "asin(", true}, {"acos", "acos(", true}, {"atan", "atan(", true},
    {"sqrt", "sqrt(", true}, {"abs", "abs(", true}, {"ln", "ln(", true}, {"log", "log(", true},
};
static const Btn s_btn2[10] = {
    {"x^2", "^2", false}, {"x^y", "^", false}, {"(", "(", false}, {")", ")", false},
    {"pi", "pi", false}, {"e", "e", false}, {"BACK", "\x01", false},
    {"CLR", "\x02", false}, {"SAVE", "\x03", false}, {"RST", "\x04", false},
};

static void btn_geometry(Window* w, int row, int col, int* x, int* y, int* bw, int* bh) {
    int n = 10;
    *bw = (w->content_w - 20 - BTN_GAP * (n - 1)) / n;
    if (*bw < 30) *bw = 30;
    *bh = BTN_H;
    *x = 10 + col * (*bw + BTN_GAP);
    *y = (row == 0 ? BTN1_Y : BTN2_Y);
}

static void insert_str(GraphState* st, const char* ins) {
    int len = (int)strlen(st->expr);
    int il = (int)strlen(ins);
    if (len + il >= MAX_EXPR - 1) return;
    for (int i = len; i >= st->cursor; i--) st->expr[i + il] = st->expr[i];
    for (int i = 0; i < il; i++) st->expr[st->cursor + i] = ins[i];
    st->cursor += il;
}

static void backspace_at(GraphState* st) {
    if (st->cursor <= 0) return;
    int len = (int)strlen(st->expr);
    for (int i = st->cursor - 1; i < len; i++) st->expr[i] = st->expr[i + 1];
    st->cursor--;
}

static void delete_at(GraphState* st) {
    int len = (int)strlen(st->expr);
    if (st->cursor >= len) return;
    for (int i = st->cursor; i < len; i++) st->expr[i] = st->expr[i + 1];
}

static void clear_expr(GraphState* st) {
    st->expr[0] = 0;
    st->cursor = 0;
}

static void save_curve(GraphState* st) {
    if (st->expr[0] == 0) return;
    if (st->nsaved >= MAX_SAVED) {
        // drop oldest saved curve to make room
        for (int i = 0; i < MAX_SAVED - 1; i++) st->saved[i] = st->saved[i + 1];
        st->nsaved--;
    }
    static const uint32_t colors[MAX_SAVED] = {0xE67E22, 0x27AE60, 0xE74C3C};
    SavedCurve& c = st->saved[st->nsaved++];
    strncpy(c.expr, st->expr, MAX_EXPR - 1);
    c.expr[MAX_EXPR - 1] = 0;
    c.color = colors[st->nsaved - 1];
}

static void draw_curve(Surface& s, const GraphState* st, const char* expr, uint32_t color) {
    if (!expr || expr[0] == 0) return;
    int pw = st->plot_w, ph = st->plot_h;
    if (pw <= 0 || ph <= 0) return;
    bool prev_valid = false;
    int prev_sy = 0;
    int jump = (ph * 2) / 3;   // vertical jump threshold: disconnect asymptotes
    for (int px = 0; px < pw; px++) {
        fix xv = px_to_x(st, px);
        fix yv = 0;
        bool derr = false;
        bool ok = fxexpr_eval(expr, xv, &yv, &derr, 0);
        if (!ok || derr) { prev_valid = false; continue; }
        // skip wildly out-of-range values (fx saturates on tan etc.)
        if (yv < st->vymin - fx::fx_mul(st->vymax - st->vymin, itofix(8)) ||
            yv > st->vymax + fx::fx_mul(st->vymax - st->vymin, itofix(8))) {
            prev_valid = false;
            continue;
        }
        int sy = y_to_px(st, yv);
        if (sy < st->plot_y || sy >= st->plot_y + ph) {
            prev_valid = false;
            continue;
        }
        if (prev_valid && sy - prev_sy < jump && prev_sy - sy < jump) {
            gfx::line(s, px + st->plot_x - 1, prev_sy, px + st->plot_x, sy, color);
        } else {
            gfx::pixel(s, px + st->plot_x, sy, color);
        }
        prev_valid = true;
        prev_sy = sy;
    }
}

// ---- paint ----
static void paint(Window* w) {
    Surface& s = w->back;
    GraphState* st = gs_of(w);
    int cw = w->content_w, ch = w->content_h;

    // background
    s.fill(0x00ECEBE6);

    // --- input box ---
    gfx::fillrect(s, 8, INPUT_Y, cw - 16, INPUT_H, 0x00FFFFFF);
    gfx::rect(s, 8, INPUT_Y, cw - 16, INPUT_H, 0x00C9C8C2);
    gfx::text(s, 12, INPUT_Y + 7, "y =", 0x00B03A2E, 0x00FFFFFF);
    int tx = 12 + 4 * 8;   // "y =" is 3 chars * 8px + one space
    // caret: draw expr before caret, caret block, expr after caret
    char head[MAX_EXPR], tail[MAX_EXPR];
    head[0] = 0; tail[0] = 0;
    strncpy(head, st->expr, st->cursor < MAX_EXPR - 1 ? st->cursor : MAX_EXPR - 1);
    head[st->cursor < MAX_EXPR - 1 ? st->cursor : MAX_EXPR - 1] = 0;
    strncpy(tail, st->expr + st->cursor, MAX_EXPR - 1 - (st->cursor < MAX_EXPR - 1 ? st->cursor : MAX_EXPR - 1));
    tail[MAX_EXPR - 1 - (st->cursor < MAX_EXPR - 1 ? st->cursor : MAX_EXPR - 1)] = 0;
    gfx::text(s, tx, INPUT_Y + 7, head, 0x001A1B1C, 0x00FFFFFF);
    int hw = gfx::text_width(head);
    gfx::fillrect(s, tx + hw, INPUT_Y + 7, 6, 16, 0x002F6FB6);
    gfx::text(s, tx + hw + 8, INPUT_Y + 7, tail, 0x001A1B1C, 0x00FFFFFF);
    // parse status
    int perr = ERR_NONE, epos = 0;
    bool parse_ok = fxexpr_parse(st->expr, &perr, &epos);
    char errbuf[64];
    if (st->expr[0] == 0) {
        gfx::text(s, cw - 200, INPUT_Y + 7, T("输入表达式，如 sin(x)", "type sin(x) ..."), 0x0090A0A8, 0x00FFFFFF);
    } else if (!parse_ok) {
        const char* msg = perr == ERR_PAREN ? T("括号不匹配", "missing )") :
                          perr == ERR_UNKNOWN_FUNC ? T("未知函数", "unknown fn") :
                          perr == ERR_TOO_LONG ? T("数字过大", "number too big") :
                          T("语法错误", "syntax err");
        ksprintf(errbuf, sizeof(errbuf), "%s @%d", msg, epos);
        gfx::text(s, cw - 220, INPUT_Y + 7, errbuf, 0x00D13438, 0x00FFFFFF);
    }

    // --- buttons ---
    for (int row = 0; row < 2; row++) {
        const Btn* arr = row == 0 ? s_btn1 : s_btn2;
        for (int col = 0; col < 10; col++) {
            int x, y, bw, bh;
            btn_geometry(w, row, col, &x, &y, &bw, &bh);
            uint32_t bg = row == 0 ? 0x002F6FB6 : 0x003B4654;
            gfx::fillrect(s, x, y, bw, bh, bg);
            gfx::rect(s, x, y, bw, bh, 0x001B2733);
            const char* label = arr[col].label;
            int tw = gfx::text_width(label);
            gfx::text(s, x + (bw - tw) / 2, y + (bh - 16) / 2, label, 0x00FFFFFF, bg);
        }
    }

    // --- hint line ---
    char hint[160];
    ksprintf(hint, sizeof(hint), "%s", T(
        "滚轮:缩放  ↑↓/PgUp/PgDn:平移  BACK/CLR/SAVE/RST:编辑与保存  Esc:关闭",
        "wheel:zoom  arrows/PgUp/PgDn:pan  BACK/CLR/SAVE/RST:edit&save  Esc:close"));
    gfx::text(s, 10, HINT_Y, hint, 0x00777F88, 0x00ECEBE6);

    // --- plot area ---
    int plot_y = HINT_Y + HINT_H + 4;
    int plot_h = ch - plot_y - STAT_H - 4;
    int plot_x = 8, plot_w = cw - 16;
    if (plot_h < 40) plot_h = 40;
    st->plot_x = plot_x; st->plot_y = plot_y;
    st->plot_w = plot_w; st->plot_h = plot_h;

    gfx::fillrect(s, plot_x, plot_y, plot_w, plot_h, 0x00FDFCF8);
    gfx::rect(s, plot_x, plot_y, plot_w, plot_h, 0x00B8B4AC);

    fix xr = st->vxmax - st->vxmin;
    fix yr = st->vymax - st->vymin;
    if (xr <= 0 || yr <= 0) reset_view(st, plot_w, plot_h);

    // grid
    fix xstep = nice_step(fx::fx_div(xr, itofix(8)));
    fix ystep = nice_step(fx::fx_div(yr, itofix(8)));
    fix x0 = ceil_mult(st->vxmin, xstep);
    fix y0 = ceil_mult(st->vymin, ystep);
    int ax = plot_x + fx::fixtoi(fx::fx_div(fx::fx_mul(itofix(0) - st->vxmin, itofix(plot_w)), xr));
    int ay = y_to_px(st, itofix(0));
    char tbuf[32];
    // vertical grid lines + x tick labels
    for (fix v = x0; v <= st->vxmax; v += xstep) {
        int px = plot_x + fx::fixtoi(fx::fx_div(fx::fx_mul(v - st->vxmin, itofix(plot_w)), xr));
        gfx::vline(s, px, plot_y, plot_y + plot_h - 1, 0x00E2DFD6);
        fix_to_str(tbuf, sizeof(tbuf), v, 0, 3);
        int tw = gfx::text_width(tbuf);
        int ly = y_to_px(st, itofix(0));
        if (ly < plot_y + 12) ly = plot_y + 14;
        if (ly > plot_y + plot_h - 20) ly = plot_y + plot_h - 20;
        gfx::text(s, px - tw / 2, ly, tbuf, 0x008B877E, 0x00FDFCF8);
    }
    // horizontal grid lines + y tick labels
    for (fix v = y0; v <= st->vymax; v += ystep) {
        int py = y_to_px(st, v);
        gfx::hline(s, plot_x, plot_x + plot_w - 1, py, 0x00E2DFD6);
        fix_to_str(tbuf, sizeof(tbuf), v, 0, 3);
        int lx = plot_x + 2;
        if (ax >= plot_x && ax < plot_x + 42) lx = ax + 4;   // keep clear of the y axis
        if (v == itofix(0)) continue;   // x-axis label drawn on the axis itself
        gfx::text(s, lx, py - 8, tbuf, 0x008B877E, 0x00FDFCF8);
    }
    // axes
    if (ax >= plot_x && ax < plot_x + plot_w) gfx::vline(s, ax, plot_y, plot_y + plot_h - 1, 0x00444440);
    if (ay >= plot_y && ay < plot_y + plot_h) gfx::hline(s, plot_x, plot_x + plot_w - 1, ay, 0x00444440);

    // curves: saved first (older underneath), then the live one on top
    for (int i = 0; i < st->nsaved; i++) {
        draw_curve(s, st, st->saved[i].expr, st->saved[i].color);
    }
    draw_curve(s, st, st->expr, 0x002F6FB6);

    // mouse crosshair
    if (st->mouse_x >= plot_x && st->mouse_x < plot_x + plot_w &&
        st->mouse_y >= plot_y && st->mouse_y < plot_y + plot_h) {
        gfx::vline(s, st->mouse_x, plot_y, plot_y + plot_h - 1, 0x00C9C4BA);
        gfx::hline(s, plot_x, plot_x + plot_w - 1, st->mouse_y, 0x00C9C4BA);
    }

    // --- status bar ---
    gfx::fillrect(s, 0, ch - STAT_H, cw, STAT_H, 0x00DAD9D3);
    fix_to_str(tbuf, sizeof(tbuf), st->vxmin, 0, 3);
    char b2[32]; fix_to_str(b2, sizeof(b2), st->vxmax, 0, 3);
    char b3[32]; fix_to_str(b3, sizeof(b3), st->vymin, 0, 3);
    char b4[32]; fix_to_str(b4, sizeof(b4), st->vymax, 0, 3);
    char sb[192];
    ksprintf(sb, sizeof(sb), "x:[%s,%s] y:[%s,%s]  curves:%d/3",
             tbuf, b2, b3, b4, st->nsaved);
    gfx::text(s, 10, ch - STAT_H + 2, sb, 0x00505050, 0x00DAD9D3);
    // mouse f(x)
    if (st->mouse_x >= plot_x && st->mouse_x < plot_x + plot_w &&
        st->mouse_y >= plot_y && st->mouse_y < plot_y + plot_h) {
        fix xv = px_to_x(st, st->mouse_x - plot_x);
        fix yv = 0;
        bool derr = false;
        bool ok = fxexpr_eval(st->expr, xv, &yv, &derr, 0);
        char mxb[32]; fix_to_str(mxb, sizeof(mxb), xv, 0, 3);
        char myb[32];
        if (ok && !derr) { fix_to_str(myb, sizeof(myb), yv, 0, 4); }
        else { strcpy(myb, "?"); }
        ksprintf(sb, sizeof(sb), "x=%s  f(x)=%s", mxb, myb);
        gfx::text(s, 400, ch - STAT_H + 2, sb, 0x002F6FB6, 0x00DAD9D3);
    }
}

static void fx_paint(Window* w) { paint(w); }

// ---- input ----
static void fx_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    GraphState* st = gs_of(w);
    if (!st) return;

    if (e->keycode == KEY_BACKSPACE) { backspace_at(st); return; }
    if (e->keycode == KEY_DEL)       { delete_at(st); return; }
    if (e->keycode == KEY_LEFT)      { if (st->cursor > 0) st->cursor--; return; }
    if (e->keycode == KEY_RIGHT)     { int l = (int)strlen(st->expr); if (st->cursor < l) st->cursor++; return; }
    if (e->keycode == KEY_HOME)      { st->cursor = 0; return; }
    if (e->keycode == KEY_END)       { st->cursor = (int)strlen(st->expr); return; }
    if (e->keycode == KEY_UP)        { pan_view(st, itofix(0), fx::fxf(-1, 8)); return; }
    if (e->keycode == KEY_DOWN)      { pan_view(st, itofix(0), fx::fxf(1, 8)); return; }
    if (e->keycode == KEY_PGUP)      { pan_view(st, fx::fxf(-1, 8), itofix(0)); return; }
    if (e->keycode == KEY_PGDN)      { pan_view(st, fx::fxf(1, 8), itofix(0)); return; }
    if (e->keycode == KEY_ESC)       { g_wm->close_window(w); return; }

    // printable chars only (no single-letter hotkeys: 's','c','r' must stay
    // typable inside function names like sin/cos/exp/sqrt/abs)
    char c = e->ascii;
    if (c >= 32 && c < 127) {
        char ins[2] = {c, 0};
        insert_str(st, ins);
        return;
    }
}

static void fx_scroll(Window* w, int delta) {
    GraphState* st = gs_of(w);
    if (!st) return;
    if (delta > 0) zoom_view(st, fx::fxf(8, 10));      // zoom in
    else if (delta < 0) zoom_view(st, fx::fxf(125, 100)); // zoom out
}

static void fx_mouse(Window* w, int mx, int my, uint8_t buttons) {
    GraphState* st = gs_of(w);
    if (!st) return;
    st->mouse_x = mx;
    st->mouse_y = my;

    // button clicks
    if (buttons & 1) {
        for (int row = 0; row < 2; row++) {
            for (int col = 0; col < 10; col++) {
                int x, y, bw, bh;
                btn_geometry(w, row, col, &x, &y, &bw, &bh);
                if (mx >= x && mx < x + bw && my >= y && my < y + bh) {
                    const Btn& b = (row == 0 ? s_btn1[col] : s_btn2[col]);
                    const char* ins = b.insert;
                    switch (ins[0]) {
                    case '\x01': backspace_at(st); break;    // ⌫
                    case '\x02': clear_expr(st); break;      // CLR
                    case '\x03': save_curve(st); break;      // SAVE
                    case '\x04': reset_view(st, st->plot_w, st->plot_h); break; // RST
                    default: insert_str(st, ins); break;
                    }
                    return;
                }
            }
        }
    }
}

static void fx_close(Window* w) {
    if (w->userdata) delete (GraphState*)w->userdata;
    w->userdata = 0;
}

} // namespace

void fxgraph_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("函数绘图", "Function Grapher"), x, y, WIN_W, WIN_H);
    if (!w) return;
    GraphState* st = new GraphState();
    st->expr[0] = 0;
    st->cursor = 0;
    strcpy(st->expr, "sin(x)");
    st->cursor = (int)strlen(st->expr);
    st->nsaved = 0;
    st->mouse_x = -1;
    st->mouse_y = -1;
    reset_view(st, WIN_W - 16, WIN_H - 18 - 134 - 24);
    w->userdata = st;
    w->on_paint = fx_paint;
    w->on_key = fx_key;
    w->on_scroll = fx_scroll;
    w->on_mouse = fx_mouse;
    w->on_close = fx_close;
    g_wm->raise(w);
}

} // namespace nefu
