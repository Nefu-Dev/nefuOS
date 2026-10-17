// nefuOS date calculator.
// Three modes:
//   0 diff:    days between two dates + weekdays of each
//   1 add:     date + N days -> new date (+ weekday)
//   2 info:    weekday, day-of-year, days until "today" (RTC)
// Uses the proleptic Gregorian calendar, years 1..9999, pure integer math.
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sys/settings.h"

namespace nefu {

namespace {

const int WIN_W = 500, WIN_H = 360;
const int MAX_IN = 8;
const int NF = 7;   // y1 m1 d1 y2 m2 d2 n

struct DcState {
    char fields[NF][MAX_IN];
    int  curs[NF];
    int  focus;
    int  mode;               // 0 diff, 1 add, 2 info
    char result[512];
};

static DcState* dc_of(Window* w) { return (DcState*)w->userdata; }

static const char* s_mode_names[3] = {T("日期差", "Diff"), T("加天数", "Add Days"), T("详情", "Info")};

static bool is_leap(int y) { return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0); }
static int  dim(int y, int m) {
    static const int md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && is_leap(y)) return 29;
    return (m >= 1 && m <= 12) ? md[m - 1] : 0;
}
static bool valid(int y, int m, int d) {
    if (y < 1 || y > 9999 || m < 1 || m > 12) return false;
    return d >= 1 && d <= dim(y, m);
}
// proleptic Gregorian Julian Day Number (integer)
static long jdn(int y, int m, int d) {
    int a = (14 - m) / 12;
    int yy = y + 4800 - a;
    int mm = m + 12 * a - 3;
    return (long)d + (153L * mm + 2) / 5 + 365L * yy + yy / 4 - yy / 100 + yy / 400 - 32045L;
}
static int weekday_from_jdn(long j) { return (int)((j + 1) % 7); }   // 0=Sun..6=Sat
static const char* dow_name(int w) {
    static const char* names[7] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                    "Thursday", "Friday", "Saturday"};
    return (w >= 0 && w <= 6) ? names[w] : "?";
}
static int day_of_year(int y, int m, int d) {
    int sum = 0;
    for (int i = 1; i < m; i++) sum += dim(y, i);
    return sum + d;
}

static void run(DcState* st) {
    st->result[0] = 0;
    int y1 = atoi(st->fields[0]), m1 = atoi(st->fields[1]), d1 = atoi(st->fields[2]);
    int y2 = atoi(st->fields[3]), m2 = atoi(st->fields[4]), d2 = atoi(st->fields[5]);
    int n = atoi(st->fields[6]);
    char line[160];
    if (st->mode == 0) {
        if (!valid(y1, m1, d1) || !valid(y2, m2, d2)) {
            strcpy(st->result, T("日期不合法（年 1~9999）", "invalid date (year 1..9999)"));
            return;
        }
        long j1 = jdn(y1, m1, d1), j2 = jdn(y2, m2, d2);
        long diff = j1 > j2 ? j1 - j2 : j2 - j1;
        int w1 = weekday_from_jdn(j1), w2 = weekday_from_jdn(j2);
        ksprintf(st->result, sizeof(st->result),
                 "%d-%02d-%02d %s\n%d-%02d-%02d %s\n\n%s %ld %s",
                 y1, m1, d1, dow_name(w1),
                 y2, m2, d2, dow_name(w2),
                 T("相差", "difference"), diff, T("天", "days"));
    } else if (st->mode == 1) {
        if (!valid(y1, m1, d1)) { strcpy(st->result, T("日期不合法", "invalid date")); return; }
        if (n < -100000 || n > 100000) { strcpy(st->result, T("天数需 -100000~100000", "days -100000..100000")); return; }
        long j = jdn(y1, m1, d1) + n;
        // JDN -> calendar (Gregorian)
        long jj = j;
        long a = jj + 32044;
        long b = (4 * a + 3) / 146097;
        long c = a - 146097 * b / 4;
        long dd = (4 * c + 3) / 1461;
        long e = c - 1461 * dd / 4;
        long mm = (5 * e + 2) / 153;
        int day = (int)(e - (153 * mm + 2) / 5 + 1);
        int mon = (int)(mm + 3 - 12 * (mm / 10));
        int year = (int)(100 * b + dd - 4800 + mm / 10);
        if (year < 1 || year > 9999) { strcpy(st->result, T("结果超出范围 1~9999 年", "out of range")); return; }
        int w = weekday_from_jdn(j);
        ksprintf(st->result, sizeof(st->result), "%d-%02d-%02d %s %ld %s → %d-%02d-%02d %s",
                 y1, m1, d1, dow_name(weekday_from_jdn(jdn(y1, m1, d1))),
                 n, T("天", "days"),
                 year, mon, day, dow_name(w));
    } else {
        if (!valid(y1, m1, d1)) { strcpy(st->result, T("日期不合法", "invalid date")); return; }
        long j = jdn(y1, m1, d1);
        int w = weekday_from_jdn(j);
        int doy = day_of_year(y1, m1, d1);
        ksprintf(line, sizeof(line), "%d-%02d-%02d  %s\n%s %d / %d",
                 y1, m1, d1, dow_name(w),
                 T("年内第", "day of year"), doy, is_leap(y1) ? 366 : 365);
        strcpy(st->result, line);
        DateInfo di;
        if (platform_rtc_date(&di) && valid(di.year, di.month, di.day)) {
            long jt = jdn(di.year, di.month, di.day);
            long diff = j > jt ? j - jt : jt - j;
            char line2[96];
            ksprintf(line2, sizeof(line2), "\n%s %d-%02d-%02d：%ld 天",
                     T("距今天", "vs today"), di.year, di.month, di.day, diff);
            strcat(st->result, line2);
        }
    }
}

static void paint(Window* w) {
    Surface& s = w->back;
    DcState* st = dc_of(w);
    int cw = w->content_w, ch = w->content_h;
    s.fill(0x00E9E7E1);

    gfx::text(s, 10, 6, T("日期计算器（公历 1~9999 年）", "Date Calculator (Gregorian)"), 0x00222222, 0x00E9E7E1);

    for (int i = 0; i < 3; i++) {
        int x = 10 + i * 120;
        uint32_t bg = st->mode == i ? 0x002F6FB6 : 0x005A6A7A;
        gfx::fillrect(s, x, 26, 116, 26, bg);
        gfx::rect(s, x, 26, 116, 26, 0x001B2733);
        int tw = gfx::text_width(s_mode_names[i]);
        gfx::text(s, x + (116 - tw) / 2, 31, s_mode_names[i], 0x00FFFFFF, bg);
    }

    // two rows of inputs: (y1 m1 d1) (y2 m2 d2) ; third: n
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;
            int x = 10 + col * 150;
            int y = 64 + row * 32;
            const char* lab = col == 0 ? T("年", "Y") : (col == 1 ? T("月", "M") : T("日", "D"));
            gfx::text(s, x, y + 6, lab, 0x00808080, 0x00E9E7E1);
            gfx::fillrect(s, x + 22, y, 110, 26, 0x00FFFFFF);
            gfx::rect(s, x + 22, y, 110, 26, st->focus == idx ? 0x002F6FB6 : 0x00C9C8C2);
            char* buf = st->fields[idx];
            int cur = st->curs[idx];
            char pre[MAX_IN], post[MAX_IN];
            pre[0] = 0; post[0] = 0;
            if (cur > 0) { strncpy(pre, buf, cur); pre[cur] = 0; }
            strcpy(post, buf + cur);
            gfx::text(s, x + 26, y + 6, pre, 0x001A1B1C, 0x00FFFFFF);
            gfx::fillrect(s, x + 26 + gfx::text_width(pre), y + 6, 6, 16, 0x002F6FB6);
            gfx::text(s, x + 26 + gfx::text_width(pre) + 8, y + 6, post, 0x001A1B1C, 0x00FFFFFF);
        }
    }
    // N field
    {
        int x = 10, y = 64 + 2 * 32;
        gfx::text(s, x, y + 6, T("天数N", "Days"), 0x00808080, 0x00E9E7E1);
        gfx::fillrect(s, x + 22, y, 110, 26, 0x00FFFFFF);
        gfx::rect(s, x + 22, y, 110, 26, st->focus == 6 ? 0x002F6FB6 : 0x00C9C8C2);
        char* buf = st->fields[6];
        int cur = st->curs[6];
        char pre[MAX_IN], post[MAX_IN];
        pre[0] = 0; post[0] = 0;
        if (cur > 0) { strncpy(pre, buf, cur); pre[cur] = 0; }
        strcpy(post, buf + cur);
        gfx::text(s, x + 26, y + 6, pre, 0x001A1B1C, 0x00FFFFFF);
        gfx::fillrect(s, x + 26 + gfx::text_width(pre), y + 6, 6, 16, 0x002F6FB6);
        gfx::text(s, x + 26 + gfx::text_width(pre) + 8, y + 6, post, 0x001A1B1C, 0x00FFFFFF);
    }
    // GO
    gfx::fillrect(s, 10, 64 + 2 * 32 + 32, 132, 28, 0x0027AE60);
    gfx::rect(s, 10, 64 + 2 * 32 + 32, 132, 28, 0x001B2733);
    gfx::text(s, 10 + (132 - gfx::text_width(T("计算 GO", "GO"))) / 2, 64 + 2 * 32 + 37,
              T("计算 GO", "GO"), 0x00FFFFFF, 0x0027AE60);

    // result
    int ry = 64 + 2 * 32 + 32 + 36;
    int rh = ch - ry - 6;
    gfx::fillrect(s, 8, ry, cw - 16, rh, 0x00FDFCF8);
    gfx::rect(s, 8, ry, cw - 16, rh, 0x00B8B4AC);
    int maxw = (cw - 24) / 8;
    int line = 0;
    int idx = 0;
    int len = (int)strlen(st->result);
    while (idx < len && line < rh / 16) {
        char lb[80];
        int n2 = 0;
        while (idx < len && n2 < maxw) lb[n2++] = st->result[idx++];
        lb[n2] = 0;
        gfx::text(s, 12, ry + 4 + line * 16, lb, 0x001A1B1C, 0x00FDFCF8);
        line++;
    }
}

static void dc_paint(Window* w) { paint(w); }

static void dc_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;
    DcState* st = dc_of(w);
    if (!st) return;
    char* buf = st->fields[st->focus];
    int* cur = &st->curs[st->focus];
    if (e->keycode == KEY_BACKSPACE) {
        if (*cur > 0) {
            int l = (int)strlen(buf);
            for (int i = *cur - 1; i < l; i++) buf[i] = buf[i + 1];
            (*cur)--;
        }
        return;
    }
    if (e->keycode == KEY_LEFT) { if (*cur > 0) (*cur)--; return; }
    if (e->keycode == KEY_RIGHT) { if (*cur < (int)strlen(buf)) (*cur)++; return; }
    if (e->keycode == KEY_HOME) { *cur = 0; return; }
    if (e->keycode == KEY_END) { *cur = (int)strlen(buf); return; }
    if (e->keycode == KEY_ENTER || e->keycode == KEY_TAB) {
        if (e->keycode == KEY_ENTER) { run(st); } else { st->focus = (st->focus + 1) % NF; }
        return;
    }
    if (e->keycode == KEY_ESC) { g_wm->close_window(w); return; }
    char c = e->ascii;
    if (c >= '0' && c <= '9') {
        if ((int)strlen(buf) >= MAX_IN - 1) return;
        for (int i = (int)strlen(buf); i >= *cur; i--) buf[i + 1] = buf[i];
        buf[*cur] = c;
        (*cur)++;
    } else if (c == '-' && st->focus == 6 && buf[0] != '-') {
        if ((int)strlen(buf) >= MAX_IN - 1) return;
        for (int i = (int)strlen(buf); i >= *cur; i--) buf[i + 1] = buf[i];
        buf[*cur] = '-';
        (*cur)++;
    }
}

static void dc_mouse(Window* w, int mx, int my, uint8_t buttons) {
    DcState* st = dc_of(w);
    if (!st) return;
    if (!(buttons & 1)) return;
    int cw = w->content_w;
    // mode buttons
    for (int i = 0; i < 3; i++) {
        int x = 10 + i * 120;
        if (my >= 26 && my < 52 && mx >= x && mx < x + 116) { st->mode = i; return; }
    }
    // fields
    for (int idx = 0; idx < NF; idx++) {
        int row = idx / 3, col = idx % 3;
        int x = 10 + col * 150 + 22;
        int y = 64 + row * 32;
        if (my >= y && my < y + 26 && mx >= x && mx < x + 110) { st->focus = idx; return; }
    }
    // GO
    int gx = 10, gy = 64 + 2 * 32 + 32;
    if (my >= gy && my < gy + 28 && mx >= gx && mx < gx + 132) { run(st); return; }
}

static void dc_close(Window* w) {
    if (w->userdata) delete (DcState*)w->userdata;
    w->userdata = 0;
}

} // namespace

void datecalc_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("日期计算器", "Date Calculator"), x, y, WIN_W, WIN_H);
    if (!w) return;
    DcState* st = new DcState();
    for (int i = 0; i < NF; i++) { st->fields[i][0] = 0; st->curs[i] = 0; }
    st->focus = 0;
    st->mode = 0;
    st->result[0] = 0;
    w->userdata = st;
    w->on_paint = dc_paint;
    w->on_key = dc_key;
    w->on_mouse = dc_mouse;
    w->on_close = dc_close;
    g_wm->raise(w);
}

} // namespace nefu
