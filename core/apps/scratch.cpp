// nefuOS Scratch - a tiny Scratch-style block programming app.
//
// A from-scratch, integer-only coding playground for nefuOS: a stage with one
// cat sprite, a palette of blocks (move/turn/go-to/say/wait/repeat/clear) and
// a linear script editor.  Click a palette block to append it to the script,
// click the green flag to run, Backspace removes the last block.  The same
// integer math rules as the kernel apply (8-direction movement, no floats).
//
// See docs/scratch-integration-plan.md for the roadmap (stage coordinates,
// sprites/clones, custom blocks, save/load to NVFS).

#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"
#include <cstring>

namespace nefu {
namespace {

// ---------------- block types ----------------
enum {
    BLK_MOVE = 0,        // move N steps along dir
    BLK_TURN_R = 1,      // turn right N degrees (45-multiple)
    BLK_TURN_L = 2,      // turn left N degrees (45-multiple)
    BLK_GOTO_RANDOM = 3, // teleport to a random stage position
    BLK_SAY = 4,         // say a message for ~2 s
    BLK_WAIT = 5,        // wait N ticks
    BLK_REPEAT = 6,      // repeat the NEXT block N times
    BLK_SAY_CLEAR = 7,   // clear the speech bubble
    BLK_COUNT = 8
};

struct Block {
    int type;
    int param;
    const char* label;
};

// ---------------- palette (click to append) ----------------
static const Block k_palette[BLK_COUNT] = {
    { BLK_MOVE,        10, "move 10 steps"     },
    { BLK_TURN_R,      45, "turn right 45"     },
    { BLK_TURN_L,      45, "turn left 45"      },
    { BLK_GOTO_RANDOM,  0, "go to random"      },
    { BLK_SAY,          0, "say Hello! 2s"     },
    { BLK_WAIT,        10, "wait 0.5s"         },
    { BLK_REPEAT,      10, "repeat next x10"   },
    { BLK_SAY_CLEAR,    0, "clear speech"      },
};

// ---------------- stage geometry ----------------
const int STAGE_X = 10;
const int STAGE_Y = 10;
const int STAGE_W = 240;
const int STAGE_H = 200;

const int SCRIPT_X = 270;
const int SCRIPT_Y = 10;
const int SCRIPT_W = 320;
const int SCRIPT_H = 200;

const int PAL_Y = 220;
const int BTN_W = 138;
const int BTN_H = 34;
const int BTN_GAP = 8;

const int BAR_Y = 410;
const int BAR_H = 34;

// 8-way movement (dir 0 = right, then clockwise; 90 in Scratch = up)
static const int k_dx[8] = { 10, 7, 0, -7, -10, -7, 0, 7 };
static const int k_dy[8] = { 0, -7, -10, -7, 0, 7, 10, 7 };
static const char* k_dir_name[8] = { "right", "up-right", "up", "up-left",
                                     "left", "down-left", "down", "down-right" };

// ---------------- state ----------------
struct Sprite {
    int x, y;          // stage center
    int dir;           // 0..7
    bool visible;
    char say[40];
    int say_timer;     // ticks left to show the bubble
};

struct ScratchState {
    Sprite sprite;
    List<Block> script;
    int pos;               // script cursor while running
    bool running;
    int wait_left;         // wait command countdown
    int repeat_remaining;  // repeat-next countdown
    int repeat_target;     // script index being repeated
    int mouse_down;        // edge-triggered click detection
    uint32_t rng;
    int step_delay;        // ticks between commands
};

static uint32_t s_rand(ScratchState* st) {
    st->rng = st->rng * 1103515245u + 12345u;
    return (st->rng >> 16) & 0x7FFFu;
}

static void scratch_reset(ScratchState* st) {
    st->script.clear();
    st->pos = 0;
    st->running = false;
    st->wait_left = 0;
    st->repeat_remaining = 0;
    st->repeat_target = 0;
    st->sprite.say[0] = 0;
    st->sprite.say_timer = 0;
}

// ---------------- block execution ----------------
static void exec_block(ScratchState* st, int idx) {
    if (idx < 0 || idx >= st->script.size()) return;
    const Block& b = st->script[idx];
    Sprite& sp = st->sprite;
    switch (b.type) {
    case BLK_MOVE: {
        int nd = sp.dir & 7;
        sp.x += k_dx[nd];
        sp.y += k_dy[nd];
        if (sp.x < 14) sp.x = 14;
        if (sp.x > STAGE_W - 14) sp.x = STAGE_W - 14;
        if (sp.y < 14) sp.y = 14;
        if (sp.y > STAGE_H - 14) sp.y = STAGE_H - 14;
        break;
    }
    case BLK_TURN_R:
        sp.dir = (sp.dir + (b.param / 45)) & 7;
        break;
    case BLK_TURN_L:
        sp.dir = (sp.dir - (b.param / 45) + 8) & 7;
        break;
    case BLK_GOTO_RANDOM:
        sp.x = 20 + (int)(s_rand(st) % 200);
        sp.y = 20 + (int)(s_rand(st) % 140);
        break;
    case BLK_SAY:
        strncpy(sp.say, "Hello!", sizeof(sp.say) - 1);
        sp.say[sizeof(sp.say) - 1] = 0;
        sp.say_timer = 125;   // ~2 s at 16 ms ticks
        break;
    case BLK_SAY_CLEAR:
        sp.say[0] = 0;
        sp.say_timer = 0;
        break;
    case BLK_WAIT:
    case BLK_REPEAT:
        break;   // handled by the tick driver, not here
    }
}

// advance the running script by one step per command interval
static void scratch_tick_driver(Window* w) {
    ScratchState* st = (ScratchState*)w->userdata;
    if (!st) return;
    if (st->sprite.say_timer > 0) st->sprite.say_timer--;
    if (!st->running) return;
    if (st->step_delay > 0) { st->step_delay--; return; }
    st->step_delay = 7;   // ~110 ms per command
    if (st->wait_left > 0) { st->wait_left--; return; }
    if (st->repeat_remaining > 0) {
        exec_block(st, st->repeat_target);
        st->repeat_remaining--;
        if (st->repeat_remaining == 0) st->pos = st->repeat_target + 1;
        return;
    }
    if (st->pos < st->script.size()) {
        const Block& b = st->script[st->pos];
        exec_block(st, st->pos);
        if (b.type == BLK_WAIT) {
            st->wait_left = b.param;
        } else if (b.type == BLK_REPEAT) {
            if (st->pos + 1 < st->script.size()) {
                st->repeat_remaining = (b.param > 0 ? b.param : 1) - 1;
                st->repeat_target = st->pos + 1;
            }
        }
        st->pos++;
    } else {
        st->running = false;
    }
}

// ---------------- drawing ----------------
static void draw_cat(Surface& s, int cx, int cy, int dir) {
    // body
    gfx::fillcircle(s, cx, cy, 11, 0xF4A93A);
    gfx::circle(s, cx, cy, 11, 0x8A5A1B);
    // ears
    gfx::line(s, cx - 8, cy - 5, cx - 6, cy - 15, 0xF4A93A);
    gfx::line(s, cx - 4, cy - 9, cx - 6, cy - 15, 0xF4A93A);
    gfx::line(s, cx + 8, cy - 5, cx + 6, cy - 15, 0xF4A93A);
    gfx::line(s, cx + 4, cy - 9, cx + 6, cy - 15, 0xF4A93A);
    // eyes
    gfx::fillrect(s, cx - 5, cy - 3, 3, 3, 0x1A1A1A);
    gfx::fillrect(s, cx + 2, cy - 3, 3, 3, 0x1A1A1A);
    // whiskers + mouth
    gfx::hline(s, cx - 10, cx - 4, cy + 3, 0x8A5A1B);
    gfx::hline(s, cx + 4, cx + 10, cy + 3, 0x8A5A1B);
    // tail pointing in the movement direction
    int tx = cx - k_dx[dir & 7];
    int ty = cy - k_dy[dir & 7];
    gfx::line(s, cx, cy, tx, ty, 0xF4A93A);
}

static void draw_speech(Surface& s, int cx, int cy, const char* msg) {
    int mlen = (int)strlen(msg);
    int bw = 12 + mlen * 6;
    if (bw < 40) bw = 40;
    int bx = cx - bw / 2;
    int by = cy - 40;
    if (bx < 0) bx = 0;
    if (bx + bw > STAGE_W) bx = STAGE_W - bw;
    if (by < 0) by = 0;
    gfx::fillrect(s, bx, by, bw, 22, 0xFFFFFF);
    gfx::rect(s, bx, by, bw, 22, 0x222222);
    gfx::text(s, bx + 6, by + 4, msg, 0x111111, 0xFFFFFF);
}

static void scratch_paint(Window* w) {
    ScratchState* st = (ScratchState*)w->userdata;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0x24262B);

    // stage
    gfx::fillrect(s, STAGE_X, STAGE_Y, STAGE_W, STAGE_H, 0xE9F2FF);
    gfx::rect(s, STAGE_X, STAGE_Y, STAGE_W, STAGE_H, 0x101216);
    if (st->sprite.visible) {
        draw_cat(s, STAGE_X + st->sprite.x, STAGE_Y + st->sprite.y, st->sprite.dir);
        if (st->sprite.say_timer > 0 && st->sprite.say[0])
            draw_speech(s, STAGE_X + st->sprite.x, STAGE_Y + st->sprite.y, st->sprite.say);
    }
    char dirline[48];
    ksprintf(dirline, sizeof(dirline), "x=%d y=%d dir=%s",
             st->sprite.x, st->sprite.y, k_dir_name[st->sprite.dir & 7]);
    gfx::text(s, STAGE_X + 4, STAGE_Y + STAGE_H - 14, dirline, 0x333333, 0xE9F2FF);

    // script panel
    gfx::rect(s, SCRIPT_X, SCRIPT_Y, SCRIPT_W, SCRIPT_H, 0x101216);
    gfx::fillrect(s, SCRIPT_X + 1, SCRIPT_Y + 1, SCRIPT_W - 2, SCRIPT_H - 2, 0x1A1C22);
    gfx::text(s, SCRIPT_X + 6, SCRIPT_Y + 4, "Script:", 0x8AC4FF, 0x1A1C22);
    int ly = SCRIPT_Y + 22;
    for (int i = 0; i < st->script.size() && ly < SCRIPT_Y + SCRIPT_H - 18; i++) {
        uint32_t fg = 0xD6E2D8;
        if (st->running && i == st->pos) { fg = 0x9BFF6B; }
        else if (st->running && st->repeat_remaining > 0 && i == st->repeat_target) { fg = 0xFFD75B; }
        char line[80];
        int n = ksprintf(line, sizeof(line), "%2d  %s", i, st->script[i].label);
        if (n > 0) gfx::text(s, SCRIPT_X + 8, ly, line, fg, 0x1A1C22);
        ly += 15;
    }
    if (st->script.size() == 0)
        gfx::text(s, SCRIPT_X + 8, SCRIPT_Y + 22, "(no blocks - click the palette below)",
                  0x777A80, 0x1A1C22);

    // palette buttons (4x2 grid)
    for (int i = 0; i < BLK_COUNT; i++) {
        int col = i % 4, row = i / 4;
        int bx = STAGE_X + col * (BTN_W + BTN_GAP);
        int by = PAL_Y + row * (BTN_H + BTN_GAP);
        gfx::fillrect(s, bx, by, BTN_W, BTN_H, 0x3B6FD4);
        gfx::rect(s, bx, by, BTN_W, BTN_H, 0x223A72);
        gfx::text(s, bx + 6, by + 8, k_palette[i].label, 0xFFFFFF, 0x3B6FD4);
    }

    // bottom action bar
    int b1 = STAGE_X;
    gfx::fillrect(s, b1, BAR_Y, 120, BAR_H, 0x2F9E44);
    gfx::rect(s, b1, BAR_Y, 120, BAR_H, 0x1B5E2A);
    gfx::text(s, b1 + 10, BAR_Y + 9, "Run  (green flag)", 0xFFFFFF, 0x2F9E44);
    int b2 = b1 + 130;
    gfx::fillrect(s, b2, BAR_Y, 110, BAR_H, 0xB03A2E);
    gfx::rect(s, b2, BAR_Y, 110, BAR_H, 0x6E221B);
    gfx::text(s, b2 + 10, BAR_Y + 9, "Clear", 0xFFFFFF, 0xB03A2E);
    int b3 = b2 + 120;
    gfx::fillrect(s, b3, BAR_Y, 150, BAR_H, 0x57595F);
    gfx::rect(s, b3, BAR_Y, 150, BAR_H, 0x33353A);
    gfx::text(s, b3 + 10, BAR_Y + 9, "Backspace = remove last", 0xFFFFFF, 0x57595F);
    int b4 = b3 + 160;
    gfx::fillrect(s, b4, BAR_Y, 140, BAR_H, 0x7A5AA0);
    gfx::rect(s, b4, BAR_Y, 140, BAR_H, 0x4E3A68);
    gfx::text(s, b4 + 10, BAR_Y + 9, "Random script", 0xFFFFFF, 0x7A5AA0);
    if (st->running) gfx::text(s, b4 + 150, BAR_Y + 9, "RUNNING...", 0x9BFF6B, 0x24262B);
}

// ---------------- input ----------------
static void scratch_mouse(Window* w, int mx, int my, uint8_t buttons) {
    ScratchState* st = (ScratchState*)w->userdata;
    bool down = (buttons & 1) != 0;
    if (down && !st->mouse_down) {
        // palette blocks
        for (int i = 0; i < BLK_COUNT; i++) {
            int col = i % 4, row = i / 4;
            int bx = STAGE_X + col * (BTN_W + BTN_GAP);
            int by = PAL_Y + row * (BTN_H + BTN_GAP);
            if (mx >= bx && mx < bx + BTN_W && my >= by && my < by + BTN_H) {
                if (!st->running) {
                    st->script.push(k_palette[i]);
                    st->pos = 0;
                }
                st->mouse_down = down;
                return;
            }
        }
        // run
        if (mx >= STAGE_X && mx < STAGE_X + 120 && my >= BAR_Y && my < BAR_Y + BAR_H) {
            if (st->script.size() > 0) {
                st->running = true;
                st->pos = 0;
                st->wait_left = 0;
                st->repeat_remaining = 0;
                st->step_delay = 0;
            }
            st->mouse_down = down;
            return;
        }
        // clear
        if (mx >= STAGE_X + 130 && mx < STAGE_X + 240 && my >= BAR_Y && my < BAR_Y + BAR_H) {
            scratch_reset(st);
            st->mouse_down = down;
            return;
        }
        // random demo script
        if (mx >= STAGE_X + 250 && mx < STAGE_X + 390 && my >= BAR_Y && my < BAR_Y + BAR_H) {
            scratch_reset(st);
            int demo[] = { BLK_MOVE, BLK_TURN_R, BLK_MOVE, BLK_SAY,
                           BLK_TURN_R, BLK_MOVE, BLK_MOVE, BLK_SAY_CLEAR,
                           BLK_REPEAT, BLK_MOVE, BLK_TURN_R };
            for (unsigned int i = 0; i < sizeof(demo) / sizeof(demo[0]); i++) {
                for (int p = 0; p < BLK_COUNT; p++) {
                    if (k_palette[p].type == demo[i]) { st->script.push(k_palette[p]); break; }
                }
            }
            st->mouse_down = down;
            return;
        }
    }
    st->mouse_down = down ? 1 : 0;
}

static void scratch_key(Window* w, const KeyEvent* e) {
    ScratchState* st = (ScratchState*)w->userdata;
    if (!e->down) return;
    if (e->ascii == 8 || e->keycode == KEY_BACKSPACE) {   // backspace: remove last block
        if (!st->running && st->script.size() > 0) st->script.remove(st->script.size() - 1);
    }
    if (e->ascii == 'r' || e->ascii == 'R') {
        if (st->script.size() > 0) {
            st->running = true;
            st->pos = 0;
            st->wait_left = 0;
            st->repeat_remaining = 0;
            st->step_delay = 0;
        }
    }
    if (e->ascii == 'c' || e->ascii == 'C') scratch_reset(st);
}

static void scratch_close(Window* w) {
    delete (ScratchState*)w->userdata;
}

} // namespace

void scratch_launch() {
    ScratchState* st = new ScratchState();
    st->sprite.x = STAGE_W / 2;
    st->sprite.y = STAGE_H / 2;
    st->sprite.dir = 0;
    st->sprite.visible = true;
    st->sprite.say[0] = 0;
    st->sprite.say_timer = 0;
    st->running = false;
    st->mouse_down = 0;
    st->rng = platform_tick_ms() * 2654435761u + 97u;
    st->step_delay = 0;
    Window* w = g_wm->create_window("Scratch", 60, 60, 600, 470);
    w->userdata = st;
    w->on_paint = scratch_paint;
    w->on_tick = scratch_tick_driver;
    w->on_mouse = scratch_mouse;
    w->on_key = scratch_key;
    w->on_close = scratch_close;
    g_wm->raise(w);
}

} // namespace nefu
