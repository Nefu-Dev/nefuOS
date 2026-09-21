// nefuOS built-in weather app
// Shows simulated weather information
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../gui/widgets.h"
#include "../klib/klib.h"
#include "../platform.h"
#include <cstdio>

namespace nefu {

struct WeatherState {
    int w, h;
};

static void weather_paint(Window* win) {
    Surface& s = win->back;
    int W = win->content_w;
    int H = win->content_h;
    gfx::fillrect(s, 0, 0, W, H, 0x1e1e2e);

    int y = 30;
    gfx::text(s, 15, y, "=== Weather Forecast ===", 0x89b4fa, 0);
    y += 30;

    // Simulated weather data
    const char* cities[] = {"Beijing", "Shanghai", "Shenzhen", "Guangzhou", "Chengdu"};
    const char* conditions[] = {"Sunny", "Cloudy", "Rainy", "Overcast", "Foggy"};
    int temps[] = {28, 25, 23, 26, 20};
    int humidity[] = {45, 65, 85, 75, 90};

    for (int i = 0; i < 5; i++) {
        char buf[64];
        ksprintf(buf, sizeof(buf), "%-12s %-10s", cities[i], conditions[i]);
        gfx::text(s, 15, y, buf, 0xa6e3a1, 0);
        y += 20;
        ksprintf(buf, sizeof(buf), "  Temp: %d C  Humidity: %d%%", temps[i], humidity[i]);
        gfx::text(s, 30, y, buf, 0xcdd6f4, 0);
        y += 28;
    }

    y += 10;
    gfx::text(s, 15, y, "--- 5-day forecast ---", 0x6c7086, 0);
    y += 25;

    const char* days[] = {"Mon", "Tue", "Wed", "Thu", "Fri"};
    for (int d = 0; d < 5; d++) {
        gfx::text(s, 20 + d * 70, y, days[d], 0xf9e2af, 0);
        char tbuf[16];
        ksprintf(tbuf, sizeof(tbuf), "%d/%dC", 25-d, 18-d);
        gfx::text(s, 20 + d * 70, y + 18, tbuf, 0xcdd6f4, 0);
    }

    y += 55;
    gfx::text(s, 15, y, "Note: Simulated demo data.", 0x6c7086, 0);
}

void app_weather_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Weather", x, y, 350, 380);
    if (!w) return;
    WeatherState* st = new WeatherState();
    st->w = w->content_w;
    st->h = w->content_h;
    w->userdata = st;
    w->on_paint = weather_paint;
    g_wm->raise(w);
}

} // namespace nefu
