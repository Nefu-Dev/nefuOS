// nefuOS Lock Screen (screensaver / lock)
// Full-screen wallpaper with time/date, click to unlock

#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../klib/klib.h"
#include "../platform.h"
#include <cstring>

namespace nefu {

namespace {

struct LockState {
    int frame;
    bool unlocked;
    String password;
    bool showPassword;

    LockState() : frame(0), unlocked(false), showPassword(false) {}
};

// Draw a simple wallpaper (gradient + stars)
static void drawWallpaper(Surface& s, int w, int h) {
    // Background gradient (dark blue to black)
    for (int y = 0; y < h; y++) {
        int r = 10 + (y * 20 / h);
        int g = 15 + (y * 30 / h);
        int b = 40 + (y * 60 / h);
        uint32_t color = (r << 16) | (g << 8) | b;
        gfx::fillrect(s, 0, y, w, 1, color);
    }

    // Draw some "stars" (random dots)
    int seed = 12345;
    for (int i = 0; i < 50; i++) {
        seed = seed * 1103515245 + 12345;
        int sx = (seed >> 16) % w;
        seed = seed * 1103515245 + 12345;
        int sy = (seed >> 16) % (h / 2);
        uint32_t brightness = 200 + (i % 55);
        uint32_t starColor = (brightness << 16) | (brightness << 8) | brightness;
        gfx::putpixel(s, sx, sy, starColor);
    }

    // Draw moon circle
    int moonX = w - 100;
    int moonY = 80;
    int moonR = 30;
    for (int dy = -moonR; dy <= moonR; dy++) {
        for (int dx = -moonR; dx <= moonR; dx++) {
            if (dx*dx + dy*dy <= moonR*moonR) {
                gfx::putpixel(s, moonX + dx, moonY + dy, 0xFFFFCC);
            }
        }
    }
}

static void lock_paint(Window* w) {
    LockState* st = (LockState*)w->userdata;
    Surface& s = w->back;

    int wW = w->content_w;
    int hH = w->content_h;

    // Draw wallpaper
    drawWallpaper(s, wW, hH);

    // Draw time (large)
    char timeStr[32];
    ksprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
             (platform_tick_ms() / 3600000) % 24,
             (platform_tick_ms() / 60000) % 60,
             (platform_tick_ms() / 1000) % 60);

    // Draw time centered
    int tw = gfx::text_width(timeStr);
    gfx::text(s, (wW - tw) / 2, hH / 2 - 60, timeStr, 0xFFFFFF, 0x00000000);

    // Draw date
    const char* dateStr = "nefuOS";
    int dw = gfx::text_width(dateStr);
    gfx::text(s, (wW - dw) / 2, hH / 2 - 30, dateStr, 0xCCCCCC, 0x00000000);

    // Draw "click to unlock" hint
    const char* hint = "Click or press any key to unlock";
    int hw = gfx::text_width(hint);
    gfx::text(s, (wW - hw) / 2, hH / 2 + 60, hint, 0x999999, 0x00000000);

    // Draw nefuOS logo text
    const char* logo = "nefuOS";
    int lw = gfx::text_width(logo);
    gfx::text(s, (wW - lw) / 2, 30, logo, 0x66CCFF, 0x00000000);
}

static void lock_mouse(Window* w, int mx, int my, uint8_t buttons) {
    if (buttons) {
        // Click to unlock
        g_wm->close_window(w);
    }
}

static void lock_key(Window* w, const KeyEvent* e) {
    if (e->down) {
        // Any key to unlock
        g_wm->close_window(w);
    }
}

static void lock_close(Window* w) {
    LockState* st = (LockState*)w->userdata;
    delete st;
}

} // namespace

void lockscreen_launch() {
    LockState* st = new LockState();

    // Create full-screen window
    Window* w = g_wm->create_window("Lock Screen", 0, 0, 800, 600);
    w->userdata = st;
    w->on_paint = lock_paint;
    w->on_mouse = lock_mouse;
    w->on_key = lock_key;
    w->on_close = lock_close;

    // Make it borderless / fullscreen
    w->maximized = true;

    g_wm->raise(w);
}

} // namespace nefu