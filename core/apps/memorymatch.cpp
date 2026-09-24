// nefuOS Memory Match — flip cards, find pairs
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

namespace {

const int MM_ROWS = 4, MM_COLS = 4;

struct MemMatch {
    int w, h;
    int cards[MM_ROWS * MM_COLS];   // symbol id (0..7), -1 = matched
    bool face[MM_ROWS * MM_COLS];   // face up
    int flipped[2];                 // indices of current flip
    int nflip;
    int moves;
    int pairs_found;
    uint32_t last_flip_ms;
    bool locked;
};

void mm_new(MemMatch& g) {
    int n = MM_ROWS * MM_COLS;
    uint32_t seed = platform_tick_ms() ^ 0x9E3779B9;
    // two of each symbol
    for (int i = 0; i < n; i++) g.cards[i] = i / 2;
    // shuffle
    for (int i = n - 1; i > 0; i--) {
        seed = seed * 1103515245 + 12345;
        int j = (int)((seed >> 16) % (uint32_t)(i + 1));
        int t = g.cards[i]; g.cards[i] = g.cards[j]; g.cards[j] = t;
    }
    for (int i = 0; i < n; i++) { g.face[i] = false; }
    g.nflip = 0;
    g.moves = 0;
    g.pairs_found = 0;
    g.locked = false;
}

// card symbol glyphs (draw simple shapes by id)
static const char* MM_SYMS[8] = {"A", "B", "C", "D", "E", "F", "G", "H"};
static const uint32_t MM_COLORS[8] = {
    0x00E74C3C, 0x002E86DE, 0x0027AE60, 0x00F39C12,
    0x008E44AD, 0x0018A79E, 0x00D35400, 0x007F8C8D
};

void mm_paint(Window* win) {
    MemMatch* g = (MemMatch*)win->userdata;
    Surface& s = win->back;
    s.fill(0x002C3E50);
    char buf[64];
    ksprintf(buf, sizeof(buf), "Memory Match  moves: %d  pairs: %d/8", g->moves, g->pairs_found);
    gfx::text(s, 8, 6, buf, color::WHITE, 0x002C3E50);
    int gap = 8;
    int cw = (g->w - gap * (MM_COLS + 1)) / MM_COLS;
    int ch = (g->h - 34 - gap * (MM_ROWS + 1)) / MM_ROWS;
    if (cw < 20) cw = 20;
    if (ch < 20) ch = 20;
    for (int r = 0; r < MM_ROWS; r++) {
        for (int c = 0; c < MM_COLS; c++) {
            int idx = r * MM_COLS + c;
            int x = gap + c * (cw + gap);
            int y = 30 + gap + r * (ch + gap);
            if (g->cards[idx] < 0) {
                // matched: subtle green box
                gfx::fillrect(s, x, y, cw, ch, 0x001B7A3B);
            } else if (g->face[idx]) {
                gfx::fillrect(s, x, y, cw, ch, 0x00ECF0F1);
                gfx::text_scale(s, x + cw / 2 - 6, y + ch / 2 - 10, MM_SYMS[g->cards[idx] & 7],
                                MM_COLORS[g->cards[idx] & 7], 0x00ECF0F1, 2);
            } else {
                gfx::fillrect(s, x, y, cw, ch, 0x003A5A80);
                gfx::rect(s, x, y, cw, ch, 0x005C7A9A);
            }
        }
    }
    gfx::text(s, 8, g->h - 14, "Click to flip  R = restart", 0x0088AACC, 0x002C3E50);
}

void mm_mouse(Window* w, int mx, int my, uint8_t buttons) {
    MemMatch* g = (MemMatch*)w->userdata;
    if (!buttons) return;
    if (g->locked) return;
    int gap = 8;
    int cw = (g->w - gap * (MM_COLS + 1)) / MM_COLS;
    int ch = (g->h - 34 - gap * (MM_ROWS + 1)) / MM_ROWS;
    if (cw < 20) cw = 20;
    if (ch < 20) ch = 20;
    for (int r = 0; r < MM_ROWS; r++) {
        for (int c = 0; c < MM_COLS; c++) {
            int x = gap + c * (cw + gap);
            int y = 30 + gap + r * (ch + gap);
            if (mx >= x && mx < x + cw && my >= y && my < y + ch) {
                int idx = r * MM_COLS + c;
                if (g->cards[idx] < 0 || g->face[idx]) return;
                g->face[idx] = true;
                g->flipped[g->nflip++] = idx;
                if (g->nflip == 2) {
                    g->moves++;
                    g->last_flip_ms = platform_tick_ms();
                    g->locked = true;
                }
                return;
            }
        }
    }
}

void mm_tick(Window* w) {
    MemMatch* g = (MemMatch*)w->userdata;
    if (!g->locked) return;
    if (platform_tick_ms() - g->last_flip_ms >= 900) {
        int a = g->flipped[0], b = g->flipped[1];
        if (g->cards[a] == g->cards[b]) {
            g->cards[a] = g->cards[b] = -1;
            g->pairs_found++;
        } else {
            g->face[a] = g->face[b] = false;
        }
        g->nflip = 0;
        g->locked = false;
    }
}

void mm_key(Window* w, const KeyEvent* e) {
    MemMatch* g = (MemMatch*)w->userdata;
    if (e->down && (e->ascii == 'r' || e->ascii == 'R')) mm_new(*g);
}

void mm_close(Window* w) {
    if (w->userdata) delete (MemMatch*)w->userdata;
    w->userdata = 0;
}

} // namespace

void memorymatch_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Memory Match", x, y, 380, 360);
    if (!w) return;
    MemMatch* g = new MemMatch();
    g->w = w->content_w;
    g->h = w->content_h;
    g->flipped[0] = g->flipped[1] = 0;
    mm_new(*g);
    w->userdata = g;
    w->on_paint = mm_paint;
    w->on_mouse = mm_mouse;
    w->on_tick = mm_tick;
    w->on_key = mm_key;
    w->on_close = mm_close;
    g_wm->raise(w);
}

} // namespace nefu
