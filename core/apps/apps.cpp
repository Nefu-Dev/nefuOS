// nefuOS app registration & common utils
#include "apps.h"
#include "email.h"
#include "gamecenter.h"
#include "clock_timer.h"
#include "converter.h"

// (forward declarations moved inside nefu namespace)
#include "../gui/gfx.h"
#include "../platform.h"
#include "../sys/settings.h"

namespace nefu {
void inputmethod_launch();
void clipboard_launch();
void notifcenter_launch();
void shortcut_launch();
void wallpaper_launch();
void theme_launch();
void processlist_launch();
void about_launch_full();
void sysinfo_extended_launch();
void credits_launch();
void releasenotes_launch();
void installer_launch();
void email_launch();
void gamecenter_launch();
void widgets_launch();

void converter_launch() { apps::converter_launch(); }

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
    case APP_CLOCK_TIMER: return T("时钟", "Clock");
    case APP_NOTEPAD: return T("记事本", "Notepad");
    case APP_MINER: return T("扫雷", "Minesweeper");
    case APP_IMAGEVIEWER: return T("图片查看器", "Image Viewer");
    case APP_MUSIC: return T("音乐播放器", "Music Player");
    case APP_MONITOR: return T("系统监视器", "System Monitor");
    case APP_BROWSER: return T("浏览器", "Browser");
    case APP_NETCFG: return T("网络", "Network");
    case APP_NEFUD: return T("应用启动器", "App Launcher");
    case APP_FONTVIEW: return T("字体查看器", "Font Viewer");
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
    case APP_INPUTMETHOD: return T("输入法", "Input Method");
    case APP_CLIPBOARD: return T("剪贴板管理器", "Clipboard");
    case APP_NOTIFCENTER: return T("通知中心", "Notifications");
    case APP_SHORTCUTS: return T("快捷键", "Shortcuts");
    case APP_WALLPAPER: return T("壁纸设置", "Wallpaper");
    case APP_THEME: return T("主题设置", "Theme");
    case APP_PROCESSLIST: return T("进程列表", "Process List");
    case APP_ABOUTFULL: return T("关于", "About");
    case APP_SYSINFOEXT: return T("系统信息(扩展)", "System Info Ext");
    case APP_CREDITS: return T("致谢", "Credits");
    case APP_RELEASENOTES: return T("更新日志", "Release Notes");
    case APP_INSTALLER: return T("安装向导", "Installer");
    case APP_EMAIL: return T("邮箱", "Email");
    case APP_GAMECENTER: return T("游戏中心", "Game Center");
    case APP_WIDGETS: return T("小组件", "Widgets");
    case APP_CONVERTER: return T("单位换算", "Unit Converter");
    case APP_TETRIS: return T("俄罗斯方块", "Tetris");
    case APP_GAME2048: return T("2048", "2048");
    case APP_SUDOKU: return T("数独", "Sudoku");
    case APP_MEMORYMATCH: return T("记忆翻牌", "Memory Match");
    case APP_HEXEDIT: return T("十六进制编辑器", "Hex Editor");
    case APP_JSONVIEW: return T("JSON 查看器", "JSON Viewer");
    case APP_FINDFILES: return T("文件查找", "Find Files");
    case APP_POMODORO: return T("番茄钟", "Pomodoro");
    case APP_STOPWATCH: return T("秒表", "Stopwatch");
    case APP_WORDCOUNT: return T("字数统计", "Word Count");
    case APP_ALGOVIZ:   return T("排序可视化", "Sort Visualizer");
    case APP_GFXLAB:    return T("图形实验室", "Gfx Lab");
    case APP_SIMLAB:    return T("仿真实验室", "Sim Lab");
#ifdef NEFU_LVGL_DEMO
    case APP_LVGLDEMO: return T("LVGL 演示", "LVGL Demo");
#endif
    case APP_VIDEOPLAYER: return T("视频播放器", "Video Player");
    case APP_TEXTTOOL:  return T("文本工具", "Text Tool");
    case APP_CRYPTOLAB: return T("密码学实验室", "Crypto Lab");
    case APP_COMPRESSTOOL: return T("压缩工具", "Compression Tool");
    case APP_SERIALAB:  return T("格式转换台", "Serialab");
    case APP_AUDIOLAB:  return T("音频实验室", "AudioLab");
    case APP_GFX3DVIEW: return T("3D 查看器", "3D Viewer");
    case APP_MATHTOOL:  return T("数学工具箱", "Math Toolbox");
    case APP_WIDGETGALLERY: return T("UI 组件库", "Widget Gallery");
    case APP_DBMANAGER: return T("数据库管理", "DB Manager");
    case APP_BREAKOUT: return T("打砖块", "Breakout");
    case APP_PONG: return T("乒乓球", "Pong");
    case APP_FLAPPY: return T("像素鸟", "Flappy");
    case APP_SPACEINV: return T("太空侵略者", "Space Invaders");
    case APP_PACMAN: return T("吃豆人", "Pacman");
    case APP_TICTACTOE: return T("井字棋", "Tic-Tac-Toe");
    case APP_CONNECT4: return T("四子棋", "Connect Four");
    case APP_MINESWEEPER2: return T("高级扫雷", "Minesweeper Pro");
    case APP_LIFE: return T("生命游戏", "Game of Life");
    case APP_MLLAB: return T("机器学习实验室", "ML Lab");`r`n    case APP_REPL: return T("迷你解释器", "Mini REPL");`r`n    case APP_SYSMON: return T("系统监视器", "SysMon");`r`n    case APP_NETLAB: return T("网络实验室", "NetLab");`r`n    case APP_FSVIEW: return T("文件系统查看器", "FSView");`r`n    case APP_RAYVIEW: return T("光线追踪", "RayView");`r`n    case APP_COMPILERLAB: return T("编译器实验室", "CompilerLab");
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
    if (strcmp(name, "Email") == 0) return APP_EMAIL;
    if (strcmp(name, "Game Center") == 0) return APP_GAMECENTER;
    if (strcmp(name, "Widgets") == 0) return APP_WIDGETS;
    if (strcmp(name, "Unit Converter") == 0) return APP_CONVERTER;
#ifdef NEFU_LVGL_DEMO
    if (strcmp(name, "LVGL Demo") == 0) return APP_LVGLDEMO;
#endif
    if (strcmp(name, "Video Player") == 0) return APP_VIDEOPLAYER;
    return -1;
}

// ---------- install state ----------
static bool s_installed[APP_COUNT];

// store apps that ship pre-installed (like a real OS ships its apps)
static const int PREINSTALLED[] = {
    APP_SNAKE, APP_PAINT, APP_CLOCK, APP_NOTEPAD, APP_MINER,
    APP_IMAGEVIEWER, APP_MUSIC, APP_VIDEOPLAYER, APP_MONITOR, APP_BROWSER, APP_NETCFG, APP_NEFUD,
    APP_FONTVIEW, APP_EDITOR,
    APP_TETRIS, APP_GAME2048, APP_SUDOKU, APP_MEMORYMATCH,
    APP_HEXEDIT, APP_JSONVIEW, APP_FINDFILES, APP_POMODORO, APP_STOPWATCH, APP_WORDCOUNT,
    APP_ALGOVIZ, APP_GFXLAB, APP_SIMLAB, APP_TEXTTOOL,
    APP_CRYPTOLAB, APP_COMPRESSTOOL, APP_SERIALAB, APP_AUDIOLAB, APP_GFX3DVIEW, APP_MATHTOOL, APP_WIDGETGALLERY,
    APP_DBMANAGER, APP_BREAKOUT, APP_PONG, APP_FLAPPY, APP_SPACEINV, APP_PACMAN,
    APP_TICTACTOE, APP_CONNECT4, APP_MINESWEEPER2, APP_LIFE, APP_MLLAB
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
            // ensure sample files exist,avoid dead clicks
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
    case APP_CLOCK_TIMER:
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
    case APP_VIDEOPLAYER:
        if (app_installed(APP_VIDEOPLAYER)) video_player_launch(); else app_show_not_installed("Video Player");
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
    case APP_INPUTMETHOD: inputmethod_launch(); break;
    case APP_CLIPBOARD: clipboard_launch(); break;
    case APP_NOTIFCENTER: notifcenter_launch(); break;
    case APP_SHORTCUTS: shortcut_launch(); break;
    case APP_WALLPAPER: wallpaper_launch(); break;
    case APP_THEME: theme_launch(); break;
    case APP_PROCESSLIST: processlist_launch(); break;
    case APP_ABOUTFULL: about_launch_full(); break;
    case APP_SYSINFOEXT: sysinfo_extended_launch(); break;
    case APP_CREDITS: credits_launch(); break;
    case APP_RELEASENOTES: releasenotes_launch(); break;
    case APP_INSTALLER: installer_launch(); break;
    case APP_EMAIL: email_launch(); break;
    case APP_GAMECENTER: gamecenter_launch(); break;
    case APP_WIDGETS: widgets_launch(); break;
    case APP_CONVERTER: converter_launch(); break;
    case APP_TETRIS: tetris_launch(); break;
    case APP_GAME2048: game2048_launch(); break;
    case APP_SUDOKU: sudoku_launch(); break;
    case APP_MEMORYMATCH: memorymatch_launch(); break;
    case APP_HEXEDIT: hexedit_launch(); break;
    case APP_JSONVIEW: jsonview_launch(); break;
    case APP_FINDFILES: findfiles_launch(); break;
    case APP_POMODORO: pomodoro_launch(); break;
    case APP_STOPWATCH: stopwatch_launch(); break;
    case APP_WORDCOUNT: wordcount_launch(); break;
    case APP_ALGOVIZ: algoviz_launch(); break;
    case APP_GFXLAB: gfxlab_launch(); break;
    case APP_SIMLAB: simlab_launch(); break;
    case APP_TEXTTOOL: texttool_launch(); break;
    case APP_CRYPTOLAB: cryptolab_launch(); break;
    case APP_COMPRESSTOOL: compresstool_launch(); break;
    case APP_SERIALAB: serialab_launch(); break;
    case APP_AUDIOLAB: audiolab_launch(); break;
    case APP_GFX3DVIEW: gfx3dview_launch(); break;
    case APP_MATHTOOL: mathtool_launch(); break;
    case APP_WIDGETGALLERY: widgetgallery_launch(); break;
    case APP_DBMANAGER: dbmanager_launch(); break;
    case APP_BREAKOUT: breakout_launch(); break;
    case APP_PONG: pong_launch(); break;
    case APP_FLAPPY: flappy_launch(); break;
    case APP_SPACEINV: spaceinv_launch(); break;
    case APP_PACMAN: pacman_launch(); break;
    case APP_TICTACTOE: tictactoe_launch(); break;
    case APP_CONNECT4: connect4_launch(); break;
    case APP_MINESWEEPER2: minesweeper2_launch(); break;
    case APP_LIFE: life_launch(); break;
    case APP_MLLAB: mllab_launch(); break;`r`n    case APP_REPL: repl_launch(); break;`r`n    case APP_SYSMON: sysmon_launch(); break;`r`n    case APP_NETLAB: netlab_launch(); break;`r`n    case APP_FSVIEW: fsview_launch(); break;`r`n    case APP_RAYVIEW: rayview_launch(); break;`r`n    case APP_COMPILERLAB: compilerlab_launch(); break;
#ifdef NEFU_LVGL_DEMO
    case APP_LVGLDEMO: lvgl_demo_launch(); break;
#endif
    default: break;
    }
}


// New app launch functions
void email_launch() {
    apps::open_email();
}

void gamecenter_launch() {
    apps::open_game_center();
}

static uint32_t s_widgets_last_tick = 0;

static void widgets_paint(Window* w) {
    Surface& s = w->back;
    
    // Background
    gfx::fillrect(s, 0, 0, w->content_w, w->content_h, 0xECF0F1);
    
    // Title
    gfx::fillrect(s, 0, 0, w->content_w, 36, 0x2C3E50);
    gfx::text(s, 12, 10, "Widgets Panel", 0xFFFFFF, 0x2C3E50);
    
    // ---- Clock widget (real RTC) ----
    gfx::fillrect(s, 20, 60, 200, 120, 0xFFFFFF);
    gfx::rect(s, 20, 60, 200, 120, 0xBDC3C7);
    gfx::text(s, 30, 70, "Clock", 0x2C3E50, 0xFFFFFF);
    DateInfo di;
    char time_buf[16], date_buf[24];
    if (platform_rtc_date(&di)) {
        ksprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", di.hour, di.min, di.sec);
        static const char* DOWS[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const char* dow = (di.dow >= 0 && di.dow <= 6) ? DOWS[di.dow] : "";
        ksprintf(date_buf, sizeof(date_buf), "%s %d-%02d-%02d", dow, di.year, di.month, di.day);
    } else {
        ksprintf(time_buf, sizeof(time_buf), "--:--:--");
        ksprintf(date_buf, sizeof(date_buf), "RTC unavailable");
    }
    gfx::text(s, 30, 100, time_buf, 0x3498DB, 0xFFFFFF);
    gfx::text(s, 30, 130, date_buf, 0x7F8C8D, 0xFFFFFF);
    
    // ---- Uptime widget (real) ----
    gfx::fillrect(s, 240, 60, 200, 120, 0xFFFFFF);
    gfx::rect(s, 240, 60, 200, 120, 0xBDC3C7);
    gfx::text(s, 250, 70, "Uptime", 0x2C3E50, 0xFFFFFF);
    uint32_t up_s = nefuos_uptime_ms() / 1000;
    char up_buf[48];
    ksprintf(up_buf, sizeof(up_buf), "%u h %02u m %02u s", up_s / 3600, (up_s % 3600) / 60, up_s % 60);
    gfx::text(s, 250, 100, up_buf, 0x27AE60, 0xFFFFFF);
    char up2_buf[48];
    ksprintf(up2_buf, sizeof(up2_buf), "since boot");
    gfx::text(s, 250, 130, up2_buf, 0x7F8C8D, 0xFFFFFF);
    
    // ---- Memory widget (real) ----
    gfx::fillrect(s, 20, 200, 200, 120, 0xFFFFFF);
    gfx::rect(s, 20, 200, 200, 120, 0xBDC3C7);
    gfx::text(s, 30, 210, "Memory", 0x2C3E50, 0xFFFFFF);
    uint32_t mu = 0, mt = 0;
    platform_mem_stats(&mu, &mt);
    int mpc = mt > 0 ? (int)((uint64_t)mu * 100 / mt) : 0;
    if (mpc > 100) mpc = 100;
    gfx::fillrect(s, 30, 240, 160, 20, 0xECF0F1);
    gfx::fillrect(s, 30, 240, (int)((uint64_t)160 * mpc / 100), 20, 0xE67E22);
    char mem_buf[48];
    ksprintf(mem_buf, sizeof(mem_buf), "%d%%  (%u KB)", mpc, mu / 1024);
    gfx::text(s, 30, 270, mem_buf, 0xE67E22, 0xFFFFFF);
    
    // ---- System widget ----
    gfx::fillrect(s, 240, 200, 200, 120, 0xFFFFFF);
    gfx::rect(s, 240, 200, 200, 120, 0xBDC3C7);
    gfx::text(s, 250, 210, "System", 0x2C3E50, 0xFFFFFF);
    HwInfo hwi;
    if (platform_hw_info(&hwi)) {
        gfx::text(s, 250, 240, hwi.cpu_model, 0xF39C12, 0xFFFFFF);
        char sys_buf[48];
        ksprintf(sys_buf, sizeof(sys_buf), "%u MB RAM / %u cores", (uint32_t)hwi.mem_total_mb, hwi.cpu_cores);
        gfx::text(s, 250, 270, sys_buf, 0x2C3E50, 0xFFFFFF);
    } else {
        gfx::text(s, 250, 240, "unknown", 0xF39C12, 0xFFFFFF);
        gfx::text(s, 250, 270, "no HW info", 0x2C3E50, 0xFFFFFF);
    }
    
    // Hint
    gfx::text(s, 20, w->content_h - 30, "Live widgets - values refresh automatically", 0x95A5A6, 0xECF0F1);
}

static void widgets_tick(Window* w) {
    if (!w) return;
    uint32_t now = platform_tick_ms();
    if (now - s_widgets_last_tick < 500) return;  // refresh every 0.5 s
    s_widgets_last_tick = now;
    widgets_paint(w);
}

void widgets_launch() {
    Window* w = g_wm->create_window("Widgets", 100, 100, 460, 360);
    if (!w) return;
    s_widgets_last_tick = 0;
    widgets_paint(w);
    w->on_paint = widgets_paint;
    w->on_tick = widgets_tick;
    
    g_wm->raise(w);
}

} // namespace nefu
