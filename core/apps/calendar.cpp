// nefuOS Calendar App
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include <cstring>

namespace nefu {

namespace {

struct CalState {
    int year;
    int month;
    int today;
    CalState() : year(2026), month(8), today(21) {}
};

static int days_in_month(int year, int month) {
    int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 1) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month];
}

static int day_of_week(int year, int month, int day) {
    if (month < 2) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + 13*(month+1)/5 + k + k/4 + j/4 + 5*j) % 7;
    return (h + 6) % 7;
}

static void cal_paint(Window* w) {
    CalState* st = (CalState*)w->userdata;
    Surface& s = w->back;
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xF0F0F0);
    char title[32];
    ksprintf(title, sizeof(title), "%d-%02d", st->year, st->month + 1);
    gfx::text(s, 10, 10, title, 0x000000, 0xF0F0F0);
    const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    int cw = (w->content_w - 20) / 7;
    for (int i = 0; i < 7; i++) {
        gfx::text(s, 10 + i * cw + 10, 35, days[i], 0x666666, 0xF0F0F0);
    }
    int first_dow = day_of_week(st->year, st->month, 1);
    int dim = days_in_month(st->year, st->month);
    int start_y = 55;
    int ch = 35;
    for (int d = 1; d <= dim; d++) {
        int dow = (first_dow + d - 1) % 7;
        int row = (first_dow + d - 1) / 7;
        int x = 10 + dow * cw + 10;
        int y = start_y + row * ch;
        char num[8];
        ksprintf(num, sizeof(num), "%d", d);
        if (d == st->today) {
            gfx::fillrect(s, x - 5, y - 2, cw - 10, ch - 5, 0x4A90D9);
            gfx::text(s, x, y, num, 0xFFFFFF, 0x4A90D9);
        } else {
            gfx::text(s, x, y, num, 0x000000, 0xF0F0F0);
        }
    }
}

static void cal_close(Window* w) {
    CalState* st = (CalState*)w->userdata;
    delete st;
}

} // namespace

void calendar_launch() {
    CalState* st = new CalState();
    Window* w = g_wm->create_window("Calendar", 150, 150, 300, 300);
    w->userdata = st;
    w->on_paint = cal_paint;
    w->on_close = cal_close;
    g_wm->raise(w);
}

} // namespace nefu