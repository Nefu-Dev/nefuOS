// nefuOS app store - LVGL GUI (catalog + npkg online registry, install/open)
#include "apps.h"
#include "npkg.h"
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
    { APP_TETRIS,  "Tetris",       "Classic falling blocks: rotate, clear lines",  "56 KB" },
    { APP_GAME2048,"2048",         "Slide & merge tiles to reach 2048",            "38 KB" },
    { APP_SUDOKU,  "Sudoku",       "9x9 puzzle with generator & solver",           "44 KB" },
    { APP_MEMORYMATCH, "Memory Match", "Flip cards and find all pairs",             "30 KB" },
    { APP_HEXEDIT, "Hex Editor",   "Inspect and edit raw file bytes",               "62 KB" },
    { APP_JSONVIEW,"JSON Viewer",  "Parse and browse JSON documents",               "40 KB" },
    { APP_FINDFILES,"Find Files",  "Recursive search with wildcard patterns",       "36 KB" },
    { APP_POMODORO,"Pomodoro",     "Focus timer with work/break cycles",            "28 KB" },
    { APP_STOPWATCH,"Stopwatch",   "Precision timer with lap recording",            "22 KB" },
    { APP_WORDCOUNT,"Word Count",  "Lines, words, chars and bytes of a file",       "20 KB" },
    { APP_ALGOVIZ, "Sort Visualizer", "Watch 8 sorting algorithms animate live",    "34 KB" },
    { APP_GFXLAB,  "Gfx Lab",      "8 live demos: raster, transforms, noise, color","42 KB" },
    { APP_SIMLAB,  "Sim Lab",      "Game of Life, particles, pendulum, L-systems, boids", "48 KB" },
    { APP_TEXTTOOL, "Text Tool",     "Edit distance, LCS, word freq and phonetics", "28 KB" },
    { APP_CRYPTOLAB, "Crypto Lab",   "AES/DES/RC4/SHA/HMAC encrypt & hash playground", "36 KB" },
    { APP_COMPRESSTOOL, "Compression Tool", "RLE/Huffman/LZW/BWT/Arithmetic compress", "24 KB" },
    { APP_SERIALAB, "Serialab",      "CSV/INI/XML/TOML/BMP/WAV format converter", "40 KB" },
    { APP_AUDIOLAB, "AudioLab",      "Oscillator/ADSR/filter/FFT audio synthesis", "32 KB" },
    { APP_GFX3DVIEW, "3D Viewer",    "Software-rendered 3D model viewer", "44 KB" },
    { APP_MATHTOOL, "Math Toolbox", "Matrix / stats / polynomial / number theory", "32 KB" },
    { APP_WIDGETGALLERY, "Widget Gallery", "UI components: buttons/tables/charts/dialogs", "40 KB" },
    { APP_DBMANAGER, "DB Manager", "SQL console + KV store + WAL in one window", "40 KB" },
    { APP_BREAKOUT, "Breakout", "Brick breaker with power-ups and levels", "36 KB" },
    { APP_PONG, "Pong", "Classic pong with AI opponent", "24 KB" },
    { APP_FLAPPY, "Flappy Bird", "Tap to flap, avoid pipes", "28 KB" },
    { APP_SPACEINV, "Space Invaders", "Defend Earth from alien invasion", "40 KB" },
    { APP_PACMAN, "Pacman", "Maze chase with 3 ghost personalities", "48 KB" },
    { APP_TICTACTOE, "Tic-Tac-Toe", "Minimax AI with difficulty levels", "20 KB" },
    { APP_CONNECT4, "Connect Four", "Alpha-beta pruning AI", "24 KB" },
    { APP_MINESWEEPER2, "Minesweeper Pro", "Three difficulty levels + chording", "32 KB" },
    { APP_LIFE, "Game of Life", "Conway's cellular automaton with 13 presets", "28 KB" },
    { APP_MLLAB, "ML Lab", "K-Means clustering visualizer + 10 ML algorithms", "48 KB" },
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
    lv_obj_t* status_lbl;
    int mode;          // 0 = built-in catalog, 1 = npkg online registry
};

struct StoreKey {
    StoreLvState* st;
    int id;
    int action;   // 0=install, 1=uninstall, 2=open, 3=npkg install
    char name[64];
};

static void store_lv_btn(lv_event_t* e);

// ---- npkg online registry view ----
static void store_npkg_status(StoreLvState* st, const char* s) {
    if (st->status_lbl) lv_label_set_text(st->status_lbl, s);
}

static void store_npkg_install(StoreLvState* st, const char* name) {
    char msg[160];
    bool ok = npkg_gui_install(name, msg, (int)sizeof(msg));
    (void)ok;
    store_npkg_status(st, msg);
}

// Fetch the full npkg registry and rebuild the list with every package.
static void store_fetch_online(StoreLvState* st) {
    uint8_t* body = 0;
    uint32_t blen = 0;
    if (!npkg_gui_fetch_registry(&body, &blen) || !body) {
        store_npkg_status(st, "npkg: fetch failed (network?)");
        return;
    }
    int n = npkg_gui_registry_count(body, blen);
    if (n <= 0) {
        kfree(body);
        store_npkg_status(st, "npkg: empty registry");
        return;
    }
    st->mode = 1;
    if (st->list) lv_obj_clean(st->list);
    for (int i = 0; i < n && i < 512; i++) {
        char name[64], ver[32], intro[160];
        name[0] = 0; ver[0] = 0; intro[0] = 0;
        if (!npkg_gui_registry_entry(body, blen, i, name, sizeof(name),
                                     ver, sizeof(ver), intro, sizeof(intro))) continue;
        int cy = i * 58;
        lv_obj_t* card = lv_obj_create(st->list);
        lv_obj_set_pos(card, 8, cy + 2);
        lv_obj_set_size(card, 496, 52);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xF5F5F0), 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0xE0DFD9), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 6, 0);
        lv_obj_set_style_pad_all(card, 0, 0);

        lv_obj_t* icon = lv_obj_create(card);
        lv_obj_set_pos(icon, 6, 6);
        lv_obj_set_size(icon, 40, 40);
        lv_obj_set_style_bg_color(icon, lv_color_hex(0x2E6F9E), 0);
        lv_obj_set_style_radius(icon, 4, 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        char ic[2] = { (char)('a' + (i % 26)), 0 };
        lv_obj_t* icl = lv_label_create(icon);
        lv_label_set_text(icl, ic);
        lv_obj_center(icl);
        lv_obj_set_style_text_color(icl, lv_color_hex(0xFFFFFF), 0);

        lv_obj_t* nlabel = lv_label_create(card);
        lv_label_set_text(nlabel, name);
        lv_obj_set_pos(nlabel, 54, 4);
        lv_obj_set_style_text_color(nlabel, lv_color_hex(0x15181E), 0);

        char dv[192];
        ksprintf(dv, sizeof(dv), "%s  v%s", intro[0] ? intro : name, ver);
        lv_obj_t* dlabel = lv_label_create(card);
        lv_label_set_text(dlabel, dv);
        lv_obj_set_pos(dlabel, 54, 22);
        lv_obj_set_style_text_color(dlabel, lv_color_hex(0x667788), 0);

        // Install button
        StoreKey* k = new StoreKey();
        k->st = st;
        k->id = i;
        k->action = 3;
        int j = 0;
        while (name[j] && j < (int)sizeof(k->name) - 1) { k->name[j] = name[j]; j++; }
        k->name[j] = 0;
        lv_obj_t* b = lv_button_create(card);
        lv_obj_set_pos(b, 400, 12);
        lv_obj_set_size(b, 82, 28);
        lv_obj_set_style_radius(b, 5, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x3D4B66), 0);
        lv_obj_add_event_cb(b, store_lv_btn, LV_EVENT_CLICKED, k);
        lv_obj_t* lbl = lv_label_create(b);
        lv_label_set_text(lbl, "Install");
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    }
    kfree(body);
    char stm[96];
    ksprintf(stm, sizeof(stm), "npkg online: %d package(s) from npkg.site.je", n);
    store_npkg_status(st, stm);
}

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
    } else if (k->action == 3) {
        // npkg online: download + install from the registry
        store_npkg_install(k->st, k->name);
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
    st->mode = 0;
    st->status_lbl = 0;
    lw->userdata = st;

    // header
    lv_obj_t* hdr = card_label(lw->content, "Software Store", 12, 6, 0x3366AA);
    (void)hdr;
    // npkg online toggle
    {
        lv_obj_t* b = lv_button_create(lw->content);
        lv_obj_set_pos(b, 340, 4);
        lv_obj_set_size(b, 90, 24);
        lv_obj_set_style_radius(b, 5, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2E6F9E), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1D4E75), LV_STATE_PRESSED);
        struct NpkgBtn { static void cb(lv_event_t* e) {
            StoreLvState* s = (StoreLvState*)lv_event_get_user_data(e);
            if (s) store_fetch_online(s);
        } };
        lv_obj_add_event_cb(b, NpkgBtn::cb, LV_EVENT_CLICKED, st);
        lv_obj_t* lbl = lv_label_create(b);
        lv_label_set_text(lbl, "npkg Online");
        lv_obj_center(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    }
    // status line
    st->status_lbl = lv_label_create(lw->content);
    lv_label_set_text(st->status_lbl, "built-in catalog (network optional)");
    lv_obj_set_pos(st->status_lbl, 12, 336);
    lv_obj_set_style_text_color(st->status_lbl, lv_color_hex(0x5588CC), 0);

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
    card_label(lw->content, "Built-in apps install instantly; 'npkg Online' fetches the full remote registry.", 12, 350, 0x5588CC);
}
} // namespace nefu
