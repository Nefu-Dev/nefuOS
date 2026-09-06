// nefuOS settings：primary color / wallpaper / clock / taskbar，changes take effect and persist
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../sys/settings.h"

namespace nefu {

struct SettingsState {
    Window* win;
    Button btns[12];
    Button* cur;
    uint8_t last_buttons;
    int hover;
};

static const char* ACCENT_NAMES[4] = { "Blue", "Green", "Purple", "Orange" };
static const char* WALL_NAMES[3] = { "Classic", "Sunset", "Dark" };

static void settings_click(void* ud) {
    SettingsState* st = (SettingsState*)ud;
    if (!st->cur) return;
    int id = st->cur->id;
    if (id >= 0 && id <= 3) {
        g_settings.accent = id;
    } else if (id >= 4 && id <= 6) {
        g_settings.wallpaper = id - 4;
    } else if (id == 7) {
        g_settings.show_clock = !g_settings.show_clock;
    } else if (id == 8) {
        g_settings.show_taskbar = !g_settings.show_taskbar;
    } else if (id == 9) {
        g_settings.lang = 0;   // English
    } else if (id == 10) {
        g_settings.lang = 1;   // Chinese
    }
    settings_save();
}

static void settings_paint(Window* w) {
    SettingsState* st = (SettingsState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    int y = 10;
    gfx::text(s, 12, y, "Personalize nefuOS", color::BLUE, color::WHITE);
    y += 26;

    gfx::text(s, 12, y, "Accent color:", color::TEXT, color::WHITE);
    y += 24;
    for (int i = 0; i < 4; i++) {
        Button& b = st->btns[i];
        b.x = 12 + i * 92;
        b.y = y;
        b.w = 84; b.h = 26;
        b.label = ACCENT_NAMES[i];
        b.id = i;
        b.on_click = settings_click;
        b.ud = st;
        ui::draw_button(s, b);
        if (g_settings.accent == i) gfx::rect(s, b.x - 2, b.y - 2, b.w + 4, b.h + 4, color::BLUE);
    }
    y += 40;

    gfx::text(s, 12, y, "Wallpaper:", color::TEXT, color::WHITE);
    y += 24;
    for (int i = 0; i < 3; i++) {
        Button& b = st->btns[4 + i];
        b.x = 12 + i * 92;
        b.y = y;
        b.w = 84; b.h = 26;
        b.label = WALL_NAMES[i];
        b.id = 4 + i;
        b.on_click = settings_click;
        b.ud = st;
        ui::draw_button(s, b);
        if (g_settings.wallpaper == i) gfx::rect(s, b.x - 2, b.y - 2, b.w + 4, b.h + 4, color::BLUE);
    }
    y += 40;

    Button& tb = st->btns[7];
    tb.x = 12; tb.y = y; tb.w = 160; tb.h = 26;
    tb.label = g_settings.show_clock ? "[x] Show clock" : "[ ] Show clock";
    tb.id = 7;
    tb.on_click = settings_click;
    tb.ud = st;
    ui::draw_button(s, tb);
    y += 40;

    // language
    gfx::text(s, 12, y, "Language:", color::TEXT, color::WHITE);
    y += 24;
    for (int i = 0; i < 2; i++) {
        Button& b = st->btns[9 + i];
        b.x = 12 + i * 92;
        b.y = y;
        b.w = 84; b.h = 26;
        b.label = (i == 0) ? "English" : "Chinese";
        b.id = 9 + i;
        b.on_click = settings_click;
        b.ud = st;
        ui::draw_button(s, b);
        if (g_settings.lang == i) gfx::rect(s, b.x - 2, b.y - 2, b.w + 4, b.h + 4, color::BLUE);
    }
    y += 40;

    gfx::text(s, 12, y, "Changes apply immediately and are saved.", color::TEXT2, color::WHITE);
    y += 20;
    gfx::text(s, 12, y, "Config: /etc/settings.conf", color::TEXT2, color::WHITE);
}

static void settings_mouse(Window* w, int mx, int my, uint8_t buttons) {
    SettingsState* st = (SettingsState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    for (int i = 0; i < 12; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;
}

static void settings_close(Window* w) {
    if (w->userdata) delete (SettingsState*)w->userdata;
    w->userdata = 0;
}

void settings_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("Settings", x, y, 420, 330);
    if (!w) return;
    SettingsState* st = new SettingsState();
    st->win = w;
    st->cur = 0;
    st->last_buttons = 0;
    st->hover = -1;
    w->userdata = st;
    w->on_paint = settings_paint;
    w->on_mouse = settings_mouse;
    w->on_close = settings_close;
}

} // namespace nefu
