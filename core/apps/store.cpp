// nefuOS app store - LVGL GUI (scrollable catalog, install/uninstall/open)
#include "apps.h"
#include "../gui/lvgl_win.h"

namespace nefu {

struct StoreItem {
    int id;
    const char* name;
    const char* desc;
    const char* size;
};

static const StoreItem CATALOG[] = {
    { APP_SNAKE,   "Snake",       "Classic snake game, arrows to move",          "48 KB" },
    { APP_PAINT,   "Paint",       "Draw with mouse, save to /home/user/Pictures","96 KB" },
    { APP_CLOCK,   "Clock",       "Analog clock with live second hand",          "24 KB" },
    { APP_NOTEPAD, "Notepad",     "Text editor with save",                        "64 KB" },
    { APP_MINER,   "Minesweeper", "Classic minesweeper, 9x9 with 10 mines",       "52 KB" },
    { APP_IMAGEVIEWER, "Image Viewer", "PPM image viewer, zoom & gallery",        "88 KB" },
    { APP_MUSIC,   "Music Player", "Playlist, progress bar & spectrum",           "72 KB" },
    { APP_MONITOR, "System Monitor", "CPU / RAM / disk live curves",              "40 KB" },
    { APP_BROWSER, "Browser",     "Web browser: file:// and http:// pages",      "128 KB" },
    { APP_NETCFG,  "Network",     "NIC status, link test, ping gateway",          "56 KB" },
    { APP_NEFUD,   "App Launcher",".nefud package viewer & launcher",             "32 KB" },
};
static const int CATALOG_N = (int)(sizeof(CATALOG) / sizeof(CATALOG[0]));

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
    int ids[8];
    int cnt = app_installed_list(ids, 8);
    int pos = 0;
    for (int i = 0; i < cnt; i++) {
        int n = ksprintf(buf + pos, (size_t)(sizeof(buf) - pos), "%d\n", ids[i]);
        pos += n;
    }
    FSNode* f = g_vfs->resolve("/etc/store.conf");
    if (!f) {
        g_vfs->mkdir("/etc");
        f = g_vfs->create_file("/etc/store.conf");
    }
    if (f) g_vfs->write_file(f, (const uint8_t*)buf, (uint32_t)pos);
}

struct StoreLvState {
    LvglWin* lw;
    lv_obj_t* list;
};

struct StoreKey {
    StoreLvState* st;
    int id;
    int action;   // 0=install, 1=uninstall, 2=open
};

static void store_lv_btn(lv_event_t* e) {
    StoreKey* k = (StoreKey*)lv_event_get_user_data(e);
    if (!k || !k->st) return;
    int id = k->id;
    if (k->action == 0) {
        app_set_installed(id, true);
        store_save();
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
        lv_obj_delete((lv_obj_t*)lv_event_get_target(e));
    } else if (k->action == 1) {
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
        lv_obj_delete((lv_obj_t*)lv_event_get_target(e));
    } else {
        app_launch(id);
    }
}

static void store_lv_card(lv_event_t* e) {
    StoreKey* k = (StoreKey*)lv_event_get_user_data(e);
    if (k && app_installed(k->id)) app_launch(k->id);
}

static lv_obj_t* store_lv_mkbtn(StoreLvState* st, lv_obj_t* parent, const char* label,
                                int x, int y, int id, int action, uint32_t bg) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 82, 28);
    lv_obj_set_style_radius(b, 5, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2B3347), LV_STATE_PRESSED);
    StoreKey* k = new StoreKey();
    k->st = st;
    k->id = id;
    k->action = action;
    lv_obj_add_event_cb(b, store_lv_btn, LV_EVENT_CLICKED, k);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    return b;
}

static lv_obj_t* card_label(lv_obj_t* parent, const char* text, int x, int y, uint32_t c) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

void store_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Software Store", x, y, 520, 360);
    if (!lw) return;
    StoreLvState* st = new StoreLvState();
    st->lw = lw;
    lw->userdata = st;

    // header
    lv_obj_t* hdr = card_label(lw->content, "Software Store", 12, 6, 0x3366AA);
    (void)hdr;

    // scrollable list
    st->list = lv_obj_create(lw->content);
    lv_obj_set_pos(st->list, 0, 30);
    lv_obj_set_size(st->list, 520, 300);
    lv_obj_set_style_bg_color(st->list, lv_color_hex(0xEDF0F6), 0);
    lv_obj_set_style_border_width(st->list, 0, 0);
    lv_obj_set_style_pad_all(st->list, 0, 0);
    lv_obj_set_scrollbar_mode(st->list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(st->list, LV_DIR_VER);

    for (int i = 0; i < CATALOG_N; i++) {
        const StoreItem& it = CATALOG[i];
        int cy = i * 58;
        lv_obj_t* card = lv_obj_create(st->list);
        lv_obj_set_pos(card, 8, cy + 2);
        lv_obj_set_size(card, 496, 52);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xF5F5F0), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0xE0DFD9), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        // icon block
        lv_obj_t* icon = lv_obj_create(card);
        lv_obj_set_pos(icon, 6, 6);
        lv_obj_set_size(icon, 40, 40);
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x3E87B5), 0);
        lv_obj_set_style_radius(icon, 4, 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        char ic[2] = { (char)('A' + i), 0 };
        lv_obj_t* icl = lv_label_create(icon);
        lv_label_set_text(icl, ic);
        lv_obj_center(icl);
        lv_obj_set_style_text_color(icl, lv_color_hex(0xFFFFFF), 0);

        card_label(card, it.name, 54, 4, 0x15181E);
        card_label(card, it.desc, 54, 22, 0x667788);
        card_label(card, it.size, 54, 37, 0x8899AA);

        bool installed = app_installed(it.id);
        if (installed) {
            store_lv_mkbtn(st, card, "Open", 400, 12, it.id, 2, 0x2E7D4F);
            store_lv_mkbtn(st, card, "Uninstall", 312, 12, it.id, 1, 0xB04A3A);
        } else {
            store_lv_mkbtn(st, card, "Install", 400, 12, it.id, 0, 0x3D4B66);
        }
    }
    // hint
    card_label(lw->content, "Installed apps appear in the Start menu. Scroll the list.", 12, 334, 0x5588CC);
}
} // namespace nefu