// nefuOS Calendar app
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"

namespace nefu {

struct CalendarState {
    int w, h;
    int year, month;
};

static int days_in_month(int y, int m) {
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        return leap ? 29 : 28;
    }
    return dim[m - 1];
}

static int day_of_week(int y, int m, int d) {
    if (m < 3) { m += 12; y--; }
    int k = y % 100;
    int j = y / 100;
    return (d + 13*(m+1)/5 + k + k/4 + j/4 + 5*j) % 7;
}

static void calendar_paint(Window* win) {
    CalendarState* st = (CalendarState*)win->userdata;
    Surface& cs = win->back;
    cs.fill(0x00F5F5F0);

    char title[64];
    ksprintf(title, sizeof(title), "Calendar - %d/%d", st->year, st->month);
    gfx::text(cs, 10, 8, title, color::TEXT, 0x00F5F5F0);

    const char* days[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    int cell_w = (st->w - 20) / 7;
    int start_y = 32;

    for (int i = 0; i < 7; i++) {
        int x = 10 + i * cell_w + (cell_w - gfx::text_width(days[i])) / 2;
        gfx::text(cs, x, start_y, days[i], 0x00555555, 0x00F5F5F0);
    }

    int first_dow = day_of_week(st->year, st->month, 1);
    int dim = days_in_month(st->year, st->month);
    int row = 1;
    for (int d = 1; d <= dim; d++) {
        int dow = (first_dow + d - 1) % 7;
        int x = 10 + dow * cell_w + (cell_w - 20) / 2;
        int y = start_y + 20 + row * 24;
        char buf[8];
        ksprintf(buf, sizeof(buf), "%d", d);
        uint32_t bg = (dow == 0 || dow == 6) ? 0x00E8E8E0 : 0x00FFFFFF;
        gfx::fillrect(cs, 10 + dow * cell_w + 2, start_y + 16 + row * 24 - 2, cell_w - 4, 22, bg);
        gfx::text(cs, x, y, buf, color::TEXT, bg);
        if (dow == 6) row++;
    }

    gfx::text(cs, 10, st->h - 24, "nefuOS Calendar", 0x00888888, 0x00F5F5F0);
}

void calendar_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Calendar", x, y, 260, 260);
    if (!w) return;
    CalendarState* st = new CalendarState();
    st->w = w->content_w;
    st->h = w->content_h;
    uint32_t now = platform_tick_ms() / 1000;
    st->year = 2024 + now / 31536000;
    st->month = (now / 2592000) % 12 + 1;
    w->userdata = st;
    w->on_paint = calendar_paint;
    g_wm->raise(w);
}

} // namespace nefu
