// nefuOS Stopwatch — lap timing
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"

namespace nefu {

namespace {

struct Stopwatch {
    int w, h;
    uint32_t start_ms;
    uint32_t elapsed_ms;     // accumulated (paused)
    bool running;
    int laps[12];
    int lap_count;
    Button btns[3];
    uint8_t last_buttons;
};

void sw_paint(Window* win) {
    Stopwatch* g = (Stopwatch*)win->userdata;
    Surface& s = win->back;
    s.fill(0x00101C28);
    gfx::text_scale(s, 12, 10, "Stopwatch", 0x0068C0E8, 0x00101C28, 2);
    uint32_t total = g->elapsed_ms + (g->running ? platform_tick_ms() - g->start_ms : 0);
    int ms = (int)(total % 1000) / 100;
    int sec = (int)(total / 1000) % 60;
    int min = (int)(total / 60000) % 60;
    int hr = (int)(total / 3600000);
    char buf[64];
    ksprintf(buf, sizeof(buf), "%02d:%02d:%02d.%d", hr, min, sec, ms);
    gfx::text_scale(s, g->w / 2 - 78, 60, buf, color::WHITE, 0x00101C28, 3);
    // buttons
    int bw = (g->w - 30) / 3;
    g->btns[0].x = 8; g->btns[0].y = 120; g->btns[0].w = bw; g->btns[0].h = 30;
    g->btns[0].label = g->running ? "Pause" : "Start"; g->btns[0].id = 0;
    g->btns[1].x = 16 + bw; g->btns[1].y = 120; g->btns[1].w = bw; g->btns[1].h = 30;
    g->btns[1].label = "Lap"; g->btns[1].id = 1;
    g->btns[2].x = 24 + 2 * bw; g->btns[2].y = 120; g->btns[2].w = bw; g->btns[2].h = 30;
    g->btns[2].label = "Reset"; g->btns[2].id = 2;
    for (int i = 0; i < 3; i++) ui::draw_button(s, g->btns[i]);
    // laps
    int y = 170;
    for (int i = 0; i < g->lap_count && y < g->h - 10; i++) {
        int lsec = g->laps[i] % 60000 / 1000;
        int lms = g->laps[i] % 1000 / 100;
        ksprintf(buf, sizeof(buf), "Lap %02d:  %02d.%ds", i + 1, lsec, lms);
        gfx::text(s, 12, y, buf, 0x0088B8D8, 0x00101C28);
        y += 20;
    }
}

void sw_mouse(Window* w, int mx, int my, uint8_t buttons) {
    Stopwatch* g = (Stopwatch*)w->userdata;
    bool pressed = buttons && !g->last_buttons;
    bool released = !buttons && g->last_buttons;
    for (int i = 0; i < 3; i++) {
        if (ui::button_event(g->btns[i], mx, my, buttons, pressed, released)) {
            switch (g->btns[i].id) {
            case 0:
                if (g->running) {
                    g->elapsed_ms += platform_tick_ms() - g->start_ms;
                    g->running = false;
                } else {
                    g->start_ms = platform_tick_ms();
                    g->running = true;
                }
                break;
            case 1:
                if (g->running && g->lap_count < 12) {
                    g->laps[g->lap_count++] = (int)(g->elapsed_ms + platform_tick_ms() - g->start_ms);
                }
                break;
            case 2:
                g->elapsed_ms = 0;
                g->running = false;
                g->lap_count = 0;
                break;
            default: break;
            }
        }
    }
    g->last_buttons = buttons;
}

void sw_close(Window* w) {
    if (w->userdata) delete (Stopwatch*)w->userdata;
    w->userdata = 0;
}

} // namespace

void stopwatch_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Stopwatch", x, y, 280, 320);
    if (!w) return;
    Stopwatch* g = new Stopwatch();
    g->w = w->content_w;
    g->h = w->content_h;
    g->start_ms = 0;
    g->elapsed_ms = 0;
    g->running = false;
    g->lap_count = 0;
    g->last_buttons = 0;
    w->userdata = g;
    w->on_paint = sw_paint;
    w->on_mouse = sw_mouse;
    w->on_close = sw_close;
    g_wm->raise(w);
}

} // namespace nefu
