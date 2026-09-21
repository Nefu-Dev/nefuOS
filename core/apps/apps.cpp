// nefuOS app registration & common utils
#include "apps.h"
void calendar_launch();
void diskusage_launch();
void passgen_launch();
void sticky_launch();
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sys/settings.h"

namespace nefu {

static int s_cascade = 0;

void cascade_pos(int* x, int* y) {
    int n = (s_cascade++) % 10;
    *x = 50 + n * 28;
    *y = 40 + n * 24;
}

String node_path(FSNode* n) {
    if (!n) return String("/");
    if (n == g_vfs->root()) return String("/");
    List<String> parts;
    FSNode* cur = n;
    while (cur && cur != g_vfs->root()) {
        parts.push(cur->name);
        cur = cur->parent;
    }
    String p;
    for (int i = parts.size() - 1; i >= 0; i--) {
        p += '/';
        p += parts[i];
    }
    return p;
}

void file_to_lines(FSNode* f, List<String>& out, int max_lines) {
    if (!f || f->is_dir || f->size == 0) return;
    char* buf = (char*)kalloc((size_t)f->size + 1);
    if (!buf) return;
    memcpy(buf, f->data, f->size);
    buf[f->size] = 0;
    char* line = buf;
    for (uint32_t i = 0; i < f->size; i++) {
        if (buf[i] == '\n') {
            buf[i] = 0;
            String s = line;
            out.push(s);
            line = buf + i + 1;
            if (out.size() >= max_lines) break;
        }
    }
    if (*line && out.size() < max_lines) {
        String s = line;
        out.push(s);
    }
    kfree(buf);
}

const char* app_name(int id) {
    switch (id) {
    case APP_FILEMGR: return T("文件管理器", "File Manager");
    case APP_TERMINAL: return T("终端", "Terminal");
    case APP_CALC: return T("计算器", "Calculator");
    case APP_TEXTVIEW: return T("文本查看器", "Text Viewer");
    case APP_SYSINFO: return T("系统信息", "System Info");
    case APP_ABOUT: return T("关于 nefuOS", "About nefuOS");
    case APP_WIKI: return T("数据库维基", "Wiki");
    case APP_SETTINGS: return T("设置", "Settings");
    case APP_STORE: return T("软件商店", "Software Store");
    case APP_SNAKE: return T("贪吃蛇", "Snake");
    case APP_PAINT: return T("画图", "Paint");
    case APP_CLOCK: return T("时钟", "Clock");
    case APP_NOTEPAD: return T("记事本", "Notepad");
    case APP_MINER: return T("扫雷", "Minesweeper");
    case APP_IMAGEVIEWER: return T("图片查看器", "Image Viewer");
    case APP_MUSIC: return T("音乐播放器", "Music Player");
    case APP_MONITOR: return T("系统监视器", "System Monitor");
    case APP_BROWSER: return T("浏览器", "Browser");
    case APP_NETCFG: return T("网络", "Network");
    case APP_NEFUD: return T("应用启动器", "App Launcher");
    case APP_FONTVIEW: return "Font Viewer";
    case APP_EDITOR: return T("代码编辑器", "Code Editor");
    case APP_CALENDAR: return T("日历", "Calendar");
    case APP_DISKUSAGE: return T("磁盘分析", "Disk Usage");
    case APP_PASSGEN: return T("密码生成器", "Password Generator");
    case APP_STICKY: return T("便签", "Sticky Notes");
    case APP_SCREENSHOT: return T("截图", "Screenshot");
    case APP_COLORPICKER: return T("取色器", "Color Picker");
    case APP_SEARCH: return T("文件搜索", "File Search");
    case APP_RECYCLEBIN: return T("回收站", "Recycle Bin");
    case APP_WEATHER: return T("天气", "Weather");
    case APP_HELP: return T("帮助", "Help");
    case APP_DICTIONARY: return T("词典", "Dictionary");
    case APP_TASKMGR: return T("任务管理器", "Task Manager");
    default: return "?";
    }
}

// ---------- .nefud name -> AppId ----------
int nefud_name_to_app_id(const char* name) {
    if (!name) return -1;
    if (strcmp(name, "Calculator") == 0) return APP_CALC;
    if (strcmp(name, "Notepad") == 0) return APP_NOTEPAD;
    if (strcmp(name, "Browser") == 0) return APP_BROWSER;
    if (strcmp(name, "System Monitor") == 0) return APP_MONITOR;
    if (strcmp(name, "Terminal") == 0) return APP_TERMINAL;
    if (strcmp(name, "File Manager") == 0) return APP_FILEMGR;
    if (strcmp(name, "Paint") == 0) return APP_PAINT;
    if (strcmp(name, "Clock") == 0) return APP_CLOCK;
    if (strcmp(name, "Snake") == 0) return APP_SNAKE;
    if (strcmp(name, "Minesweeper") == 0) return APP_MINER;
    if (strcmp(name, "Image Viewer") == 0) return APP_IMAGEVIEWER;
    if (strcmp(name, "Music Player") == 0) return APP_MUSIC;
    if (strcmp(name, "Settings") == 0) return APP_SETTINGS;
    if (strcmp(name, "Software Store") == 0) return APP_STORE;
    if (strcmp(name, "Network") == 0) return APP_NETCFG;
    if (strcmp(name, "System Info") == 0) return APP_SYSINFO;
    if (strcmp(name, "About nefuOS") == 0) return APP_ABOUT;
    if (strcmp(name, "Wiki") == 0) return APP_WIKI;
    if (strcmp(name, "App Launcher") == 0) return APP_NEFUD;
    if (strcmp(name, "Font Viewer") == 0) return APP_FONTVIEW;
    return -1;
}

// ---------- install state ----------
static bool s_installed[APP_COUNT];

// store apps that ship pre-installed (like a real OS ships its apps)
static const int PREINSTALLED[] = {
    APP_SNAKE, APP_PAINT, APP_CLOCK, APP_NOTEPAD, APP_MINER,
    APP_IMAGEVIEWER, APP_MUSIC, APP_MONITOR, APP_BROWSER, APP_NETCFG, APP_NEFUD,
    APP_FONTVIEW, APP_EDITOR
};

void apps_preinstall_defaults() {
    for (size_t i = 0; i < sizeof(PREINSTALLED) / sizeof(PREINSTALLED[0]); i++) {
        s_installed[PREINSTALLED[i]] = true;
    }
}

bool app_installed(int id) {
    return true;  // ALL APPS PRE-INSTALLED
    if (id >= 0 && id < APP_BUILTIN_COUNT) return true;
    if (id < 0 || id >= APP_COUNT) return false;
    return s_installed[id];
}

void app_set_installed(int id, bool on) {
    if (id < 0 || id >= APP_COUNT) return;
    s_installed[id] = on;
}

int app_installed_list(int* ids, int max) {
    int n = 0;
    for (int i = APP_BUILTIN_COUNT; i < APP_COUNT && n < max; i++) {
        if (s_installed[i]) ids[n++] = i;
    }
    return n;
}

// ---------- about window ----------
static void about_paint(Window* w) {
    (void)w;
    Surface& s = w->back;
    s.fill(color::WHITE);
    gfx::text_scale(s, 16, 16, "nefuOS", color::BLUE, color::WHITE, 3);
    int y = 64;
    gfx::text(s, 16, y, "A tiny desktop operating system", color::TEXT, color::WHITE); y += 22;
    gfx::text(s, 16, y, "Core : C++ (GUI / VFS / Apps)", color::TEXT, color::WHITE); y += 18;
    gfx::text(s, 16, y, "Kernel: C + C++ (x86 32-bit)", color::TEXT, color::WHITE); y += 18;
    gfx::text(s, 16, y, "Boot : GRUB Multiboot", color::TEXT, color::WHITE); y += 18;
    gfx::text(s, 16, y, "ISO  : nefuOS.iso (bootable)", color::TEXT, color::WHITE); y += 18;
    gfx::text(s, 16, y, "Host : nefuOS.exe (Windows)", color::TEXT, color::WHITE); y += 18;
    gfx::text(s, 16, y, "Version 0.1.0 (c) 2026", color::TEXT2, color::WHITE);
}

static void about_close(Window* w) {
    (void)w;
}

void app_show_about() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("About nefuOS", x, y, 460, 220);
    if (!w) return;
    w->userdata = 0;
    w->on_paint = about_paint;
    w->on_close = about_close;
}

// ---------- not-installed notice ----------
static void notinst_paint(Window* w) {
    Surface& s = w->back;
    s.fill(color::WHITE);
    const char* name = (const char*)w->userdata;
    gfx::text(s, 14, 14, "Not installed", color::RED, color::WHITE);
    char buf[96];
    ksprintf(buf, sizeof(buf), "%s is not installed yet.", name ? name : "This app");
    gfx::text(s, 14, 40, buf, color::TEXT, color::WHITE);
    gfx::text(s, 14, 62, "Open Software Store to install it.", color::BLUE_LT, color::WHITE);
}

void app_show_not_installed(const char* name) {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("App", x, y, 340, 110);
    if (!w) return;
    w->userdata = (void*)name;
    w->on_paint = notinst_paint;
    w->on_close = 0;
}

// ---------- boot ----------
void app_launch(const char* name) {
    if (!name) return;
    int id = nefud_name_to_app_id(name);
    if (id < 0) {
        for (int i = 0; i < APP_COUNT; i++) {
            const char* n = app_name(i);
            if (n && strcmp(n, name) == 0) { id = i; break; }
        }
    }
    if (id >= 0) app_launch(id);
}

void app_launch(int id) {
    klogf("app_launch(%d) name=%s\n", id, app_name(id));
    switch (id) {
    case APP_FILEMGR: fm_launch(); break;
    case APP_TERMINAL: term_launch(); break;
    case APP_CALC: calc_launch(); break;
    case APP_TEXTVIEW: {
        FSNode* f = g_vfs->resolve("/home/user/Documents/nefuos.txt");
        if (!f) {
            // ensure sample files exist，avoid dead clicks
            g_vfs->mkdir("/home/user/Documents");
            f = g_vfs->create_file("/home/user/Documents/nefuos.txt");
            if (f) {
                const char* boot_txt =
                    "Welcome to nefuOS!\n"
                    "\n"
                    "nefuOS is a tiny desktop operating system\n"
                    "written in C++ with a C-flavoured core.\n"
                    "\n"
                    "Try the Terminal app:\n"
                    "  help              - list commands\n"
                    "  tree              - show the file tree\n"
                    "  cat /etc/nefu.conf - read a config file\n"
                    "  echo hi > /tmp/a.txt - write a file\n"
                    "\n"
                    "Open Software Store to install more apps!\n";
                g_vfs->write_file(f, (const uint8_t*)boot_txt, (uint32_t)strlen(boot_txt));
            }
        }
        if (f) app_show_textview(f);
        break;
    }
    case APP_SYSINFO: sysinfo_launch(); break;
    case APP_ABOUT: app_show_about(); break;
    case APP_WIKI: wiki_launch(); break;
    case APP_SETTINGS: settings_launch(); break;
    case APP_STORE: store_launch(); break;
    case APP_SNAKE:
        if (app_installed(APP_SNAKE)) snake_launch(); else app_show_not_installed("Snake");
        break;
    case APP_PAINT:
        if (app_installed(APP_PAINT)) paint_launch(); else app_show_not_installed("Paint");
        break;
    case APP_CLOCK:
        if (app_installed(APP_CLOCK)) clock_launch(); else app_show_not_installed("Clock");
        break;
    case APP_NOTEPAD:
        if (app_installed(APP_NOTEPAD)) notepad_launch(); else app_show_not_installed("Notepad");
        break;
    case APP_MINER:
        if (app_installed(APP_MINER)) miner_launch(); else app_show_not_installed("Minesweeper");
        break;
    case APP_IMAGEVIEWER:
        if (app_installed(APP_IMAGEVIEWER)) imageviewer_launch(); else app_show_not_installed("Image Viewer");
        break;
    case APP_MUSIC:
        if (app_installed(APP_MUSIC)) music_launch(); else app_show_not_installed("Music Player");
        break;
    case APP_MONITOR:
        if (app_installed(APP_MONITOR)) monitor_launch(); else app_show_not_installed("System Monitor");
        break;
    case APP_BROWSER:
        if (app_installed(APP_BROWSER)) browser_launch(); else app_show_not_installed("Browser");
        break;
    case APP_NETCFG:
        if (app_installed(APP_NETCFG)) netcfg_launch(); else app_show_not_installed("Network");
        break;
    case APP_NEFUD:
        if (app_installed(APP_NEFUD)) nefud_launch(); else app_show_not_installed("App Launcher");
        break;
    case APP_FONTVIEW:
        if (app_installed(APP_FONTVIEW)) fontview_launch(); else app_show_not_installed("Font Viewer");
        break;
    case APP_EDITOR:
        if (app_installed(APP_EDITOR)) editor_launch(); else app_show_not_installed("Code Editor");
        break;
    case APP_LVGLDEMO:
        if (app_installed(APP_LVGLDEMO)) fontview_launch(); else app_show_not_installed("LVGL Demo");
        break;
    case APP_LVGLDESKTOP:
        if (app_installed(APP_LVGLDESKTOP)) fontview_launch(); else app_show_not_installed("LVGL Desktop");
        break;
    case APP_CALENDAR:
        if (app_installed(APP_CALENDAR)) calendar_launch(); else app_show_not_installed("Calendar");
        break;
    case APP_DISKUSAGE:
        if (app_installed(APP_DISKUSAGE)) diskusage_launch(); else app_show_not_installed("Disk Usage");
        break;
    case APP_PASSGEN:
        if (app_installed(APP_PASSGEN)) passgen_launch(); else app_show_not_installed("Password Generator");
        break;
    case APP_STICKY:
        if (app_installed(APP_STICKY)) sticky_launch(); else app_show_not_installed("Sticky Notes");
        break;
    case APP_SCREENSHOT: app_screenshot_launch(); break;
    case APP_COLORPICKER: app_colorpicker_launch(); break;
    case APP_SEARCH: app_search_launch(); break;
    case APP_RECYCLEBIN: app_recyclebin_launch(); break;
    case APP_WEATHER: app_weather_launch(); break;
    case APP_HELP: app_help_launch(); break;
    case APP_DICTIONARY: app_dictionary_launch(); break;
    case APP_TASKMGR: app_taskmgr_launch(); break;
    default: break;
    }
}

} // namespace nefu
