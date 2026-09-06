// nefuOS app store：browse catalog、install/umount/open app，state persistence /etc/store.conf
#include "apps.h"
#include "../gui/gfx.h"
#include "../gui/widgets.h"
#include "../platform.h"
#include "../sys/settings.h"
#include "../vfs/vfs.h"

namespace nefu {

struct StoreItem {
    int id;
    const char* name;
    const char* desc;
    const char* size;
};

static const StoreItem CATALOG[] = {
    { APP_SNAKE,   "Snake",       "Classic snake game, arrows to move",        "48 KB" },
    { APP_PAINT,   "Paint",       "Draw with mouse, save to /home/user/Pictures", "96 KB" },
    { APP_CLOCK,   "Clock",       "Analog clock with live second hand",        "24 KB" },
    { APP_NOTEPAD, "Notepad",     "Text editor with save (Esc saves)",         "64 KB" },
    { APP_MINER,   "Minesweeper", "Classic minesweeper, 9x9 with 10 mines",    "52 KB" },
    { APP_IMAGEVIEWER, "Image Viewer", "PPM image viewer, zoom & gallery",     "88 KB" },
    { APP_MUSIC,   "Music Player", "Playlist, progress bar & spectrum",        "72 KB" },
    { APP_MONITOR, "System Monitor", "CPU / RAM / disk live curves",           "40 KB" },
    { APP_BROWSER, "Browser",       "Web browser: file:// and http:// pages", "128 KB" },
    { APP_NETCFG,  "Network",       "NIC status, link test, ping gateway",     "56 KB" },
    { APP_NEFUD,   "App Launcher",  ".nefud package viewer & launcher",         "32 KB" },
};
static const int CATALOG_N = (int)(sizeof(CATALOG) / sizeof(CATALOG[0]));

struct StoreState {
    Window* win;
    Button btns[CATALOG_N];
    Button* cur;
    uint8_t last_buttons;
    int scroll;
};

void store_load() {
    FSNode* f = g_vfs->resolve("/etc/store.conf");
    if (!f || f->is_dir || f->size == 0) return;
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            int v = atoi(line);
            if (v >= 0 && v < APP_COUNT) app_set_installed(v, true);
            line = buf + i + 1;
        }
    }
    kfree(buf);
}

void store_save() {
    char buf[128];
    int n = 0;
    int ids[8];
    int cnt = app_installed_list(ids, 8);
    int pos = 0;
    for (int i = 0; i < cnt; i++) {
        n = ksprintf(buf + pos, (size_t)(sizeof(buf) - pos), "%d\n", ids[i]);
        pos += n;
    }
    FSNode* f = g_vfs->resolve("/etc/store.conf");
    if (!f) {
        g_vfs->mkdir("/etc");
        f = g_vfs->create_file("/etc/store.conf");
    }
    if (f) g_vfs->write_file(f, (const uint8_t*)buf, (uint32_t)pos);
}

static void store_click(void* ud) {
    StoreState* st = (StoreState*)ud;
    if (!st->cur) return;
    Button& b = *st->cur;
    int id = b.id;
    if (strcmp(b.label, "Install") == 0) {
        app_set_installed(id, true);
        store_save();
        // write a real package stub into the VFS so the store state is
        // visible in the file manager (app downloads live in /usr/share/apps)
        const char* name = 0;
        for (int i = 0; i < CATALOG_N; i++) if (CATALOG[i].id == id) { name = CATALOG[i].name; break; }
        if (name) {
            char path[80];
            ksprintf(path, sizeof(path), "/usr/share/apps/%s.nefud", name);
            g_vfs->mkdir("/usr/share/apps");
            FSNode* f = g_vfs->create_file(path);
            if (f) {
                char pkg[160];
                int n = ksprintf(pkg, sizeof(pkg),
                    "type=nefud-app\nname=%s\nversion=1.0\narch=nefuOS\ninstalled=1\nlaunch-id=%d\n",
                    name, id);
                g_vfs->write_file(f, (const uint8_t*)pkg, (uint32_t)n);
            }
        }
    } else if (strcmp(b.label, "Uninstall") == 0) {
        app_set_installed(id, false);
        store_save();
        const char* name = 0;
        for (int i = 0; i < CATALOG_N; i++) if (CATALOG[i].id == id) { name = CATALOG[i].name; break; }
        if (name) {
            char path[80];
            ksprintf(path, sizeof(path), "/usr/share/apps/%s.nefud", name);
            FSNode* f = g_vfs->resolve(path);
            if (f) g_vfs->remove_node(f);
        }
    } else if (strcmp(b.label, "Open") == 0) {
        app_launch(id);
    }
}

static void store_paint(Window* w) {
    StoreState* st = (StoreState*)w->userdata;
    Surface& s = w->back;
    s.fill(color::WHITE);
    gfx::text(s, 12, 8, T("软件商店", "Software Store"), color::BLUE, color::WHITE);
    gfx::hline(s, 8, s.width - 8, 26, color::BORDER);
    int y = 34 - st->scroll * 58;
    for (int i = 0; i < CATALOG_N; i++) {
        const StoreItem& it = CATALOG[i];
        // card bottom
        if (y + 52 < 0) { y += 58; continue; }
        if (y > s.height) break;
        gfx::fillrect(s, 8, y, s.width - 16, 52, 0x00F5F5F0);
        gfx::rect(s, 8, y, s.width - 16, 52, 0x00E0DFD9);
        // name + icon color block
        gfx::fillrect(s, 14, y + 6, 40, 40, 0x003E87B5);
        char icon[2] = { (char)('A' + i), 0 };
        gfx::text(s, 28, y + 18, icon, color::WHITE, 0x003E87B5);
        gfx::text(s, 60, y + 6, it.name, color::TEXT, 0x00F5F5F0);
        gfx::text(s, 60, y + 24, it.desc, color::TEXT2, 0x00F5F5F0);
        gfx::text(s, 60, y + 40, it.size, color::TEXT2, 0x00F5F5F0);
        // button
        Button& b = st->btns[i];
        b.x = s.width - 96;
        b.y = y + 12;
        b.w = 82;
        b.h = 28;
        b.id = it.id;
        b.on_click = store_click;
        b.ud = st;
        bool installed = app_installed(it.id);
        b.label = installed ? "Uninstall" : "Install";
        ui::draw_button(s, b);
        if (installed) {
            gfx::text(s, 12, y + 42, "double-click card to open", color::TEXT2, 0x00F5F5F0);
        }
        y += 58;
    }
    gfx::text(s, 12, y + 2, T("已安装应用会出现在开始菜单中。", "Installed apps appear in the Start menu."), color::BLUE_LT, color::WHITE);
}

static void store_mouse(Window* w, int mx, int my, uint8_t buttons) {
    StoreState* st = (StoreState*)w->userdata;
    bool pressed = buttons && !st->last_buttons;
    bool released = !buttons && st->last_buttons;
    st->last_buttons = buttons;
    for (int i = 0; i < CATALOG_N; i++) {
        st->cur = &st->btns[i];
        ui::button_event(st->btns[i], mx, my, buttons, pressed, released);
    }
    st->cur = 0;
    if (!pressed) return;
    // double-click card opens app
    static int s_last_card = -1;
    static uint32_t s_last_card_t = 0;
    int y = 34 - st->scroll * 58;
    for (int i = 0; i < CATALOG_N; i++) {
        if (mx >= 10 && mx < w->content_w - 10 && my >= y && my < y + 52) {
            if (s_last_card == i && platform_tick_ms() - s_last_card_t < 400) {
                app_launch(CATALOG[i].id);
            }
            s_last_card = i;
            s_last_card_t = platform_tick_ms();
            return;
        }
        y += 58;
    }
}

static void store_scroll(Window* w, int delta);   // forward

static void store_key(Window* w, const KeyEvent* e) {
    (void)w;
    if (!e->down) return;
    // keyboard scrolling (no wheel on bare metal)
    if (e->keycode == KEY_PGDN || e->keycode == KEY_DOWN) { store_scroll(w, -1); return; }
    if (e->keycode == KEY_PGUP || e->keycode == KEY_UP) { store_scroll(w, 1); return; }
    // U = uninstall selected（simple：uninstall first installed）
    if (e->ascii == 'u' || e->ascii == 'U') {
        for (int i = 0; i < CATALOG_N; i++) {
            if (app_installed(CATALOG[i].id)) {
                app_set_installed(CATALOG[i].id, false);
                store_save();
                break;
            }
        }
    }
}

static void store_scroll(Window* w, int delta) {
    StoreState* st = (StoreState*)w->userdata;
    if (!st) return;
    // normalize platform wheel delta (+-120) to a single step
    st->scroll += (delta > 0) ? -1 : 1;
    int vis = (w->content_h - 34) / 58;
    if (vis < 1) vis = 1;
    int max_scroll = CATALOG_N - vis;
    if (max_scroll < 0) max_scroll = 0;
    if (st->scroll < 0) st->scroll = 0;
    if (st->scroll > max_scroll) st->scroll = max_scroll;
}

static void store_close(Window* w) {
    if (w->userdata) delete (StoreState*)w->userdata;
    w->userdata = 0;
}

void store_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window(T("软件商店", "Software Store"), x, y, 520, 360);
    if (!w) return;
    StoreState* st = new StoreState();
    st->win = w;
    st->cur = 0;
    st->last_buttons = 0;
    st->scroll = 0;
    w->userdata = st;
    w->on_paint = store_paint;
    w->on_mouse = store_mouse;
    w->on_key = store_key;
    w->on_scroll = store_scroll;
    w->on_close = store_close;
}

} // namespace nefu
