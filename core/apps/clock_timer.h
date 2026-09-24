// nefuOS Clock & Timer
// Clock, Stopwatch, Timer, Alarm
#pragma once
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"

namespace nefu { namespace apps {

enum class ClockMode {
    Clock,
    Stopwatch,
    Timer
};

struct ClockState {
    ClockMode mode;
    int hours, minutes, seconds;
    int stopwatch_ms;
    int timer_total;     // seconds (as set by the user)
    int timer_left_ms;   // ms remaining (counts down)
    bool running;
};

static ClockState s_clock = { ClockMode::Clock, 12, 0, 0, 0, 60, 60000, false };
static uint32_t s_clock_last_tick = 0;

static void draw_clock_digit(Surface& s, int x, int y, int digit, int size) {
    // Simple 7-segment style digit rendering
    uint32_t on = 0x2C3E50;
    uint32_t off = 0xECF0F1;
    
    int w = size;
    int h = size * 2;
    int t = size / 4;
    
    // Segments: a(up), b(right-up), c(right-down), d(down), e(left-down), f(left-up), g(middle)
    bool seg[7] = { false };
    switch (digit) {
        case 0: seg[0]=seg[1]=seg[2]=seg[3]=seg[4]=seg[5]=true; break;
        case 1: seg[1]=seg[2]=true; break;
        case 2: seg[0]=seg[1]=seg[6]=seg[4]=seg[3]=true; break;
        case 3: seg[0]=seg[1]=seg[6]=seg[2]=seg[3]=true; break;
        case 4: seg[5]=seg[6]=seg[1]=seg[2]=true; break;
        case 5: seg[0]=seg[5]=seg[6]=seg[2]=seg[3]=true; break;
        case 6: seg[0]=seg[5]=seg[6]=seg[4]=seg[2]=seg[3]=true; break;
        case 7: seg[0]=seg[1]=seg[2]=true; break;
        case 8: seg[0]=seg[1]=seg[2]=seg[3]=seg[4]=seg[5]=seg[6]=true; break;
        case 9: seg[0]=seg[1]=seg[2]=seg[3]=seg[5]=seg[6]=true; break;
    }
    
    // a (top)
    gfx::fillrect(s, x + t, y, w - 2*t, t, seg[0] ? on : off);
    // b (right top)
    gfx::fillrect(s, x + w - t, y + t, t, h/2 - t, seg[1] ? on : off);
    // c (right bottom)
    gfx::fillrect(s, x + w - t, y + h/2, t, h/2 - t, seg[2] ? on : off);
    // d (bottom)
    gfx::fillrect(s, x + t, y + h - t, w - 2*t, t, seg[3] ? on : off);
    // e (left bottom)
    gfx::fillrect(s, x, y + h/2, t, h/2 - t, seg[4] ? on : off);
    // f (left top)
    gfx::fillrect(s, x, y + t, t, h/2 - t, seg[5] ? on : off);
    // g (middle)
    gfx::fillrect(s, x + t, y + h/2 - t/2, w - 2*t, t, seg[6] ? on : off);
}

static void paint_clock(Window* w) {
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xFFFFFF);
    
    // Title
    gfx::fillrect(s, 0, 0, w->content_w, 32, 0x2C3E50);
    const char* title = s_clock.mode == ClockMode::Clock ? "Clock" :
                        s_clock.mode == ClockMode::Stopwatch ? "Stopwatch" : "Timer";
    gfx::text(s, 12, 8, title, 0xFFFFFF, 0x2C3E50);
    
    // Mode tabs
    const char* modes[] = { "Clock", "Stopwatch", "Timer" };
    for (int i = 0; i < 3; i++) {
        int bx = 12 + i * 100;
        int by = 42;
        int bw = 90;
        int bh = 28;
        
        bool active = (int)s_clock.mode == i;
        gfx::fillrect(s, bx, by, bw, bh, active ? 0x3498DB : 0xECF0F1);
        gfx::text(s, bx + 10, by + 8, modes[i], active ? 0xFFFFFF : 0x2C3E50, 
                  active ? 0x3498DB : 0xECF0F1);
    }
    
    // Display
    int cx = w->content_w / 2;
    int cy = 140;
    int digit_size = 24;
    
    int hh, mm, ss;
    
    if (s_clock.mode == ClockMode::Clock) {
        DateInfo di;
        if (platform_rtc_date(&di)) {
            hh = di.hour; mm = di.min; ss = di.sec;
        } else {
            hh = s_clock.hours;
            mm = s_clock.minutes;
            ss = s_clock.seconds;
        }
    } else if (s_clock.mode == ClockMode::Stopwatch) {
        hh = s_clock.stopwatch_ms / 3600000;
        mm = (s_clock.stopwatch_ms % 3600000) / 60000;
        ss = (s_clock.stopwatch_ms % 60000) / 1000;
    } else {
        int tl = s_clock.timer_left_ms / 1000;
        hh = tl / 3600;
        mm = (tl % 3600) / 60;
        ss = tl % 60;
    }
    
    // Draw digits
    int dx = cx - 80;
    draw_clock_digit(s, dx, cy, hh / 10, digit_size);
    draw_clock_digit(s, dx + 30, cy, hh % 10, digit_size);
    gfx::text(s, dx + 62, cy + 10, ":", 0x2C3E50, 0xFFFFFF);
    draw_clock_digit(s, dx + 75, cy, mm / 10, digit_size);
    draw_clock_digit(s, dx + 105, cy, mm % 10, digit_size);
    gfx::text(s, dx + 137, cy + 10, ":", 0x2C3E50, 0xFFFFFF);
    draw_clock_digit(s, dx + 150, cy, ss / 10, digit_size);
    draw_clock_digit(s, dx + 180, cy, ss % 10, digit_size);
    
    // Control buttons
    int btn_y = 220;
    int btn_h = 36;
    
    if (s_clock.mode == ClockMode::Stopwatch || s_clock.mode == ClockMode::Timer) {
        // Start/Stop button
        gfx::fillrect(s, cx - 110, btn_y, 100, btn_h, s_clock.running ? 0xE74C3C : 0x27AE60);
        gfx::text(s, cx - 80, btn_y + 12, s_clock.running ? "Stop" : "Start", 0xFFFFFF, 
                  s_clock.running ? 0xE74C3C : 0x27AE60);
        
        // Reset button
        gfx::fillrect(s, cx + 10, btn_y, 100, btn_h, 0x95A5A6);
        gfx::text(s, cx + 40, btn_y + 12, "Reset", 0xFFFFFF, 0x95A5A6);

        if (s_clock.mode == ClockMode::Timer) {
            // Timer duration setup buttons
            gfx::fillrect(s, cx - 110, btn_y - 44, 100, 30, 0x3498DB);
            gfx::text(s, cx - 92, btn_y - 36, "-1 min", 0xFFFFFF, 0x3498DB);
            gfx::fillrect(s, cx + 10, btn_y - 44, 100, 30, 0x3498DB);
            gfx::text(s, cx + 28, btn_y - 36, "+1 min", 0xFFFFFF, 0x3498DB);
            char set_buf[48];
            ksprintf(set_buf, sizeof(set_buf), "Set: %d min %02d s", s_clock.timer_total / 60, s_clock.timer_total % 60);
            gfx::text(s, cx - 80, btn_y + 48, set_buf, 0x7F8C8D, 0xFFFFFF);
        }
    }
    
    // Hint
    gfx::text(s, 12, w->content_h - 30, "Click tabs to switch modes", 0x95A5A6, 0xFFFFFF);
}

static void on_clock_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (!(buttons & 1)) return;
    
    // Mode tabs
    for (int i = 0; i < 3; i++) {
        int bx = 12 + i * 100;
        int by = 42;
        int bw = 90;
        int bh = 28;
        
        if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
            s_clock.mode = (ClockMode)i;
            s_clock.running = false;
            paint_clock(w);
            return;
        }
    }
    
    // Buttons
    if (s_clock.mode == ClockMode::Stopwatch || s_clock.mode == ClockMode::Timer) {
        int cx = w->content_w / 2;
        int btn_y = 220;
        int btn_h = 36;
        
        // Start/Stop
        if (mx >= cx - 110 && mx <= cx - 10 && my >= btn_y && my <= btn_y + btn_h) {
            s_clock.running = !s_clock.running;
            paint_clock(w);
            return;
        }
        
        // Reset
        if (mx >= cx + 10 && mx <= cx + 110 && my >= btn_y && my <= btn_y + btn_h) {
            if (s_clock.mode == ClockMode::Stopwatch) {
                s_clock.stopwatch_ms = 0;
            } else {
                s_clock.timer_left_ms = s_clock.timer_total * 1000;
            }
            s_clock.running = false;
            paint_clock(w);
            return;
        }

        if (s_clock.mode == ClockMode::Timer) {
            // -1 min
            if (mx >= cx - 110 && mx <= cx - 10 && my >= btn_y - 44 && my <= btn_y - 14) {
                s_clock.timer_total -= 60;
                if (s_clock.timer_total < 60) s_clock.timer_total = 60;
                s_clock.timer_left_ms = s_clock.timer_total * 1000;
                paint_clock(w);
                return;
            }
            // +1 min
            if (mx >= cx + 10 && mx <= cx + 110 && my >= btn_y - 44 && my <= btn_y - 14) {
                s_clock.timer_total += 60;
                if (s_clock.timer_total > 3600) s_clock.timer_total = 3600;
                s_clock.timer_left_ms = s_clock.timer_total * 1000;
                paint_clock(w);
                return;
            }
        }
    }
}

static void on_clock_tick(Window* w) {
    if (!w) return;
    uint32_t now = platform_tick_ms();
    uint32_t dt = (s_clock_last_tick == 0) ? 50u : (now - s_clock_last_tick);
    s_clock_last_tick = now;
    if (dt > 500) dt = 500;  // clamp after pauses

    if (s_clock.running) {
        if (s_clock.mode == ClockMode::Stopwatch) {
            s_clock.stopwatch_ms += (int)dt;
        } else if (s_clock.mode == ClockMode::Timer) {
            s_clock.timer_left_ms -= (int)dt;
            if (s_clock.timer_left_ms <= 0) {
                s_clock.timer_left_ms = 0;
                s_clock.running = false;  // countdown finished
            }
        }
        paint_clock(w);
    } else if (s_clock.mode == ClockMode::Clock) {
        // refresh the real clock every second (dt accumulated)
        paint_clock(w);
    }
}

void clock_timer_launch() {
    Window* w = g_wm->create_window("Clock & Timer", 100, 100, 360, 320);
    if (!w) return;
    s_clock_last_tick = 0;
    paint_clock(w);
    w->on_mouse = on_clock_mouse;
    w->on_paint = paint_clock;
    w->on_tick = on_clock_tick;
    g_wm->raise(w);
}

}} // namespace
