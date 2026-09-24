// nefuOS Pomodoro Timer — focus timer with work/break cycles
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

namespace {

struct Pomodoro {
    int w, h;
    int work_min, break_min;
    int remaining;         // seconds
    bool working;          // true = work phase
    bool running;
    uint32_t last_tick;
    int cycles_done;
    Button btns[4];
    uint8_t last_buttons;
};

void pom_reset(Pomodoro& g, bool start_work) {
    g.working = start_work;
    g.remaining = (start_work ? g.work_min : g.break_min) * 60;
    g.running = false;
    g.last_tick = platform_tick_ms();
}

void pom_paint(Window* win) {
    Pomodoro* g = (Pomodoro*)win->userdata;
    Surface& s = win->back;
    s.fill(0x002E2B29);
    gfx::text_scale(s, 12, 10, "Pomodoro", 0x00E8B870, 0x002E2B29, 2);
    char buf[96];
    ksprintf(buf, sizeof(buf), "Phase: %s   Cycle: %d",
             g->working ? "WORK" : "BREAK", g->cycles_done / 2);
    gfx::text(s, 12, 44, buf, color::TEXT, 0x002E2B29);
    // big timer
    int m = g->remaining / 60;
    int sec = g->remaining % 60;
    ksprintf(buf, sizeof(buf), "%02d:%02d", m, sec);
    int cx = g->w / 2;
    int cy = 130;
    // circle background
    gfx::circle(s, cx, cy, 70, g->working ? 0x00B85C3C : 0x003C7A5C);
    gfx::fillcircle(s, cx, cy, 66, g->working ? 0x00D9744C : 0x004C9A70);
    gfx::text_scale(s, cx - 44, cy - 12, buf, color::WHITE, g->working ? 0x00D9744C : 0x004C9A70, 3);
    // buttons
    int bw = (g->w - 40) / 4;
    g->btns[0].x = 10; g->btns[0].y = 230; g->btns[0].w = bw; g->btns[0].h = 30;
    g->btns[0].label = g->running ? "Pause" : "Start"; g->btns[0].id = 0;
    g->btns[1].x = 20 + bw; g->btns[1].y = 230; g->btns[1].w = bw; g->btns[1].h = 30;
    g->btns[1].label = "Reset"; g->btns[1].id = 1;
    g->btns[2].x = 30 + 2 * bw; g->btns[2].y = 230; g->btns[2].w = bw; g->btns[2].h = 30;
    g->btns[2].label = "Work 25m"; g->btns[2].id = 2;
    g->btns[3].x = 40 + 3 * bw; g->btns[3].y = 230; g->btns[3].w = bw; g->btns[3].h = 30;
    g->btns[3].label = "Break 5m"; g->btns[3].id = 3;
    for (int i = 0; i < 4; i++) ui::draw_button(s, g->btns[i]);
    ksprintf(buf, sizeof(buf), "Work: %d min   Break: %d min", g->work_min, g->break_min);
    gfx::text(s, 12, g->h - 30, buf, 0x00888888, 0x002E2B29);
    gfx::text(s, 12, g->h - 16, "1/2: adjust work  3/4: adjust break  Space: start/pause", 0x00888888, 0x002E2B29);
}

void pom_tick(Window* w) {
    Pomodoro* g = (Pomodoro*)w->userdata;
    if (!g->running) return;
    uint32_t now = platform_tick_ms();
    if (now - g->last_tick >= 1000) {
        g->last_tick = now;
        if (g->remaining > 0) {
            g->remaining--;
        }
        if (g->remaining == 0) {
            // phase switch
            if (g->working) {
                g->cycles_done++;
                g->working = false;
                g->remaining = g->break_min * 60;
            } else {
                g->working = true;
                g->remaining = g->work_min * 60;
            }
            g->running = false;
        }
    }
}

void pom_mouse(Window* w, int mx, int my, uint8_t buttons) {
    Pomodoro* g = (Pomodoro*)w->userdata;
    bool pressed = buttons && !g->last_buttons;
    bool released = !buttons && g->last_buttons;
    for (int i = 0; i < 4; i++) {
        if (ui::button_event(g->btns[i], mx, my, buttons, pressed, released)) {
            switch (g->btns[i].id) {
            case 0: g->running = !g->running; g->last_tick = platform_tick_ms(); break;
            case 1: pom_reset(*g, true); break;
            case 2: g->work_min = 25; pom_reset(*g, true); break;
            case 3: g->break_min = 5; g->working = false; g->remaining = 300; break;
            default: break;
            }
        }
    }
    g->last_buttons = buttons;
}

void pom_key(Window* w, const KeyEvent* e) {
    Pomodoro* g = (Pomodoro*)w->userdata;
    if (!e->down) return;
    if (e->ascii == ' ') { g->running = !g->running; g->last_tick = platform_tick_ms(); return; }
    if (e->ascii == '1') { if (g->work_min > 5) g->work_min -= 5; pom_reset(*g, true); return; }
    if (e->ascii == '2') { if (g->work_min < 60) g->work_min += 5; pom_reset(*g, true); return; }
    if (e->ascii == '3') { if (g->break_min > 1) g->break_min -= 1; g->remaining = g->break_min * 60; g->working = false; return; }
    if (e->ascii == '4') { if (g->break_min < 30) g->break_min += 1; g->remaining = g->break_min * 60; g->working = false; return; }
}

void pom_close(Window* w) {
    if (w->userdata) delete (Pomodoro*)w->userdata;
    w->userdata = 0;
}

} // namespace

void pomodoro_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Pomodoro Timer", x, y, 340, 300);
    if (!w) return;
    Pomodoro* g = new Pomodoro();
    g->w = w->content_w;
    g->h = w->content_h;
    g->work_min = 25;
    g->break_min = 5;
    g->cycles_done = 0;
    pom_reset(*g, true);
    g->last_buttons = 0;
    w->userdata = g;
    w->on_paint = pom_paint;
    w->on_tick = pom_tick;
    w->on_mouse = pom_mouse;
    w->on_key = pom_key;
    w->on_close = pom_close;
    g_wm->raise(w);
}

} // namespace nefu
