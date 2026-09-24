// nefuOS desktop - LVGL 9.2.0 (MIT) is now the GUI engine for the desktop
// layer: wallpaper, icon grid, taskbar, live clock, start menu and context
// menus are all LVGL objects flushed through the nefuOS Surface. stb_truetype
// (MIT) stays available for TTF text via core/gui/ttfont.cpp.
// The native window manager keeps running on top of the LVGL desktop.
#include "desktop.h"
#include "../apps/apps.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../platform.h"
#include "lv_cjk_font.h"
#include "lvgl.h"

#if !defined(NEFU_BARE) && defined(_WIN32)
#include <cstdio>
#endif

LV_FONT_DECLARE(lv_font_montserrat_16);

namespace nefu {

// lock-screen state owned by nefuos.cpp
bool nefuos_is_locked();
int nefuos_lock_len();
const char* nefuos_lock_pwd();
uint32_t nefuos_lock_fail_ms();

static const int TASKBAR_H = 30;
static const int ICON_W = 72, ICON_H = 72;
static const int TILE = 52;

// aggregate-only (no ctor): bare kernel never runs C++ static ctors
struct DesktopIcon {
    int x, y;
    char label[24];        // display label (mutable via the Rename menu item)
    int app;
    bool deleted;          // shortcut removed (app still installed)
    bool used;             // slot occupied (runtime-created shortcuts use free slots)
};

static const int GRID_COLS = 3;             // desktop icon grid
// Rows that actually fit above the taskbar on the 800x600 screen:
// (600 - 30 taskbar - 14 margin) / (72 icon + 10 gap) = 6. Icon rows beyond
// this used to be placed below the taskbar / off-screen ("apps at the bottom").
static const int GRID_ROWS = (600 - TASKBAR_H - 14) / (ICON_H + 10);
static const int MAX_ICONS = 40;            // total slots (builtin + runtime shortcuts)

static DesktopIcon s_icons[MAX_ICONS] = {
    {14, 14, "文件管理器", APP_FILEMGR, false, true},
    {14 + ICON_W + 8, 14, "终端", APP_TERMINAL, false, true},
    {14 + 2 * (ICON_W + 8), 14, "计算器", APP_CALC, false, true},
    {14, 14 + ICON_H + 10, "文本查看器", APP_TEXTVIEW, false, true},
    {14 + ICON_W + 8, 14 + ICON_H + 10, "数据库维基", APP_WIKI, false, true},
    {14, 14 + 2 * (ICON_H + 10), "设置", APP_SETTINGS, false, true},
    {14 + ICON_W + 8, 14 + 2 * (ICON_H + 10), "软件商店", APP_STORE, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 2 * (ICON_H + 10), "图片查看器", APP_IMAGEVIEWER, false, true},
    {14, 14 + 3 * (ICON_H + 10), "音乐播放器", APP_MUSIC, false, true},
    {14 + ICON_W + 8, 14 + 3 * (ICON_H + 10), "系统监视器", APP_MONITOR, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 3 * (ICON_H + 10), "浏览器", APP_BROWSER, false, true},
    {14, 14 + 4 * (ICON_H + 10), "网络", APP_NETCFG, false, true},
    {14 + ICON_W + 8, 14 + 4 * (ICON_H + 10), "应用启动器", APP_NEFUD, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 4 * (ICON_H + 10), "日历", APP_CALENDAR, false, true},
    {14, 14 + 5 * (ICON_H + 10), "磁盘分析", APP_DISKUSAGE, false, true},
    {14 + ICON_W + 8, 14 + 5 * (ICON_H + 10), "密码生成器", APP_PASSGEN, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 5 * (ICON_H + 10), "便签", APP_STICKY, false, true},
    {14, 14 + 6 * (ICON_H + 10), "邮箱", APP_EMAIL, false, true},
    {14 + ICON_W + 8, 14 + 6 * (ICON_H + 10), "游戏中心", APP_GAMECENTER, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 6 * (ICON_H + 10), "小组件", APP_WIDGETS, false, true},
    {14, 14 + 7 * (ICON_H + 10), "时钟", APP_CLOCK_TIMER, false, true},
    {14 + ICON_W + 8, 14 + 7 * (ICON_H + 10), "单位换算", APP_CONVERTER, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 7 * (ICON_H + 10), "视频播放器", APP_VIDEOPLAYER, false, true},
    {14, 14 + 8 * (ICON_H + 10), "俄罗斯方块", APP_TETRIS, false, true},
    {14 + ICON_W + 8, 14 + 8 * (ICON_H + 10), "2048", APP_GAME2048, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 8 * (ICON_H + 10), "数独", APP_SUDOKU, false, true},
    {14, 14 + 9 * (ICON_H + 10), "十六进制编辑器", APP_HEXEDIT, false, true},
    {14 + ICON_W + 8, 14 + 9 * (ICON_H + 10), "番茄钟", APP_POMODORO, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 9 * (ICON_H + 10), "记忆翻牌", APP_MEMORYMATCH, false, true},
    {14, 14 + 10 * (ICON_H + 10), "JSON 查看器", APP_JSONVIEW, false, true},
    {14 + ICON_W + 8, 14 + 10 * (ICON_H + 10), "文件查找", APP_FINDFILES, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 10 * (ICON_H + 10), "秒表", APP_STOPWATCH, false, true},
    {14, 14 + 11 * (ICON_H + 10), "字数统计", APP_WORDCOUNT, false, true},
    {14 + ICON_W + 8, 14 + 11 * (ICON_H + 10), "排序可视化", APP_ALGOVIZ, false, true},
    {14 + 2 * (ICON_W + 8), 14 + 11 * (ICON_H + 10), "图形实验室", APP_GFXLAB, false, true},
};

static const char* icon_label(int app) {
    switch (app) {
    case APP_FILEMGR:    return T("文件管理器", "Files");
    case APP_TERMINAL:   return T("终端", "Terminal");
    case APP_CALC:       return T("计算器", "Calculator");
    case APP_TEXTVIEW:   return T("文本查看器", "Text View");
    case APP_SYSINFO:    return T("系统信息", "System Info");
    case APP_ABOUT:      return T("关于", "About");
    case APP_WIKI:       return T("数据库维基", "Wiki");
    case APP_SETTINGS:   return T("设置", "Settings");
    case APP_STORE:      return T("软件商店", "Store");
    case APP_SNAKE:      return T("贪吃蛇", "Snake");
    case APP_PAINT:      return T("画图", "Paint");
    case APP_CLOCK_TIMER:      return T("时钟", "Clock");
    case APP_NOTEPAD:    return T("记事本", "Notepad");
    case APP_MINER:      return T("扫雷", "Minesweeper");
    case APP_IMAGEVIEWER:return T("图片查看器", "Images");
    case APP_MUSIC:      return T("音乐播放器", "Music");
    case APP_VIDEOPLAYER:return T("视频播放器", "Video");
    case APP_MONITOR:    return T("系统监视器", "Monitor");
    case APP_BROWSER:    return T("浏览器", "Browser");
    case APP_NETCFG:     return T("网络", "Network");
    case APP_NEFUD:      return T("应用启动器", "Launcher");
    case APP_FONTVIEW:   return T("字体查看器", "Fonts");
    case APP_EDITOR:     return T("代码编辑器", "Editor");
    case APP_CALENDAR:   return T("日历", "Calendar");
    case APP_DISKUSAGE:  return T("磁盘分析", "Disk Usage");
    case APP_PASSGEN:    return T("密码生成器", "Password");
    case APP_STICKY:     return T("便签", "Sticky Note");
    case APP_EMAIL:      return T("邮箱", "Email");
    case APP_GAMECENTER: return T("游戏中心", "Games");
    case APP_WIDGETS:    return T("小组件", "Widgets");
    case APP_SCREENSHOT: return T("截图", "Screenshot");
    case APP_COLORPICKER:return T("取色器", "Color Picker");
    case APP_SEARCH:     return T("搜索", "Search");
    case APP_RECYCLEBIN: return T("回收站", "Recycle Bin");
    case APP_WEATHER:    return T("天气", "Weather");
    case APP_HELP:       return T("帮助", "Help");
    case APP_DICTIONARY: return T("词典", "Dictionary");
    case APP_TASKMGR:    return T("任务管理器", "Task Manager");
    case APP_INPUTMETHOD:return T("输入法", "Input Method");
    case APP_CLIPBOARD:  return T("剪贴板", "Clipboard");
    case APP_NOTIFCENTER:return T("通知中心", "Notifications");
    case APP_SHORTCUTS:  return T("快捷键", "Shortcuts");
    case APP_WALLPAPER:  return T("壁纸", "Wallpaper");
    case APP_THEME:      return T("主题", "Theme");
    case APP_PROCESSLIST:return T("进程列表", "Processes");
    case APP_ABOUTFULL:  return T("关于详细", "About Full");
    case APP_SYSINFOEXT: return T("详细系统信息", "System Info Ext");
    case APP_CREDITS:    return T("致谢", "Credits");
    case APP_RELEASENOTES: return T("更新日志", "Release Notes");
    case APP_INSTALLER:  return T("安装程序", "Installer");
    case APP_TETRIS:     return T("俄罗斯方块", "Tetris");
    case APP_GAME2048:   return T("2048", "2048");
    case APP_SUDOKU:     return T("数独", "Sudoku");
    case APP_MEMORYMATCH:return T("记忆翻牌", "Memory");
    case APP_HEXEDIT:    return T("十六进制编辑器", "Hex Editor");
    case APP_JSONVIEW:   return T("JSON 查看器", "JSON");
    case APP_FINDFILES:  return T("文件查找", "Find Files");
    case APP_POMODORO:   return T("番茄钟", "Pomodoro");
    case APP_STOPWATCH:  return T("秒表", "Stopwatch");
    case APP_WORDCOUNT:  return T("字数统计", "Word Count");
    case APP_ALGOVIZ:    return T("排序可视化", "Sort Visualizer");
    case APP_GFXLAB:     return T("图形实验室", "Gfx Lab");
    case APP_SIMLAB:     return T("仿真实验室", "Sim Lab");
    case APP_TEXTTOOL:   return T("文本工具", "Text Tool");
    case APP_CRYPTOLAB:  return T("密码学实验室", "Crypto Lab");
    case APP_COMPRESSTOOL: return T("压缩工具", "Compress");
    case APP_SERIALAB:   return T("格式转换台", "Serialab");
    case APP_AUDIOLAB:   return T("音频实验室", "AudioLab");
    case APP_GFX3DVIEW:  return T("3D 查看器", "3D Viewer");
    case APP_MATHTOOL:   return T("数学工具箱", "Math Toolbox");
    case APP_WIDGETGALLERY: return T("UI 组件库", "Widgets");
    case APP_DBMANAGER: return T("数据库管理", "DB Manager");
    case APP_BREAKOUT:  return T("打砖块", "Breakout");
    case APP_PONG:      return T("乒乓球", "Pong");
    case APP_FLAPPY:    return T("像素鸟", "Flappy");
    case APP_SPACEINV:  return T("太空侵略者", "Space Invaders");
    case APP_PACMAN:    return T("吃豆人", "Pacman");
    case APP_TICTACTOE: return T("井字棋", "Tic-Tac-Toe");
    case APP_CONNECT4:  return T("四子棋", "Connect Four");
    case APP_MINESWEEPER2: return T("高级扫雷", "Minesweeper Pro");
    case APP_LIFE:      return T("生命游戏", "Game of Life");
    case APP_MLLAB:     return T("机器学习实验室", "ML Lab");
    case APP_REPL:      return T("迷你解释器", "Mini REPL");
    case APP_SYSMON:    return T("系统监视器", "SysMon");
    case APP_NETLAB:    return T("网络实验室", "NetLab");
    case APP_FSVIEW:    return T("文件系统", "FSView");
    case APP_RAYVIEW:   return T("光线追踪", "RayView");
    case APP_COMPILERLAB: return T("编译器", "Compiler");
    case APP_GEDEMO: return T("游戏引擎", "GE Demo");
    case APP_DEEPVIZ: return T("深度学习", "DeepViz");
    default: return T("未知应用", "Unknown");
    }
}

// ---- LVGL desktop engine state ----
static lv_display_t* s_disp = 0;
static lv_indev_t* s_indev = 0;
static lv_color_t* s_fb_buf = 0;
static Surface* s_surf = 0;
static lv_obj_t* s_clock_label = 0;
static lv_obj_t* s_start_menu = 0;
static lv_obj_t* s_rmenu = 0;          // icon shortcut context menu
static lv_obj_t* s_rmenu_desk = 0;     // blank-desktop context menu
static lv_obj_t* s_icon_objs[MAX_ICONS];
static lv_obj_t* s_icon_labels[MAX_ICONS];
static lv_obj_t* s_rmenu_items[8];
static lv_obj_t* s_rmenu_desk_items[8];
static int s_rmenu_icon = -1;
static bool s_start_open = false;
static bool s_rmenu_open = false;
static bool s_rmenu_desk_open = false;
static int s_rmenu_mw = 150, s_rmenu_mh = 50;
static int s_rmenu_desk_mw = 150, s_rmenu_desk_mh = 50;
static int s_mx = 10, s_my = 10;
static bool s_btn = false;
static bool s_initialized = false;
static uint8_t s_last_buttons = 0;
static uint32_t s_icon_color[40];
static bool s_dirty = false;
static uint32_t s_last_sig = 0;
static int s_menu_apps[32];
static int s_menu_count = 0;

// ---- LVGL keyboard input (keypad indev + shared group) ----
static uint32_t s_lv_keys[32];
static uint8_t s_lv_key_n = 0;
static lv_indev_t* s_kb_indev = 0;
static lv_group_t* s_kb_group = 0;

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)disp;
    if (!s_surf) {
        lv_display_flush_ready(disp);
        return;
    }
    Surface& s = *s_surf;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    // px_map is ARGB8888 (4 bytes/px) for LV_COLOR_DEPTH=32; read as uint32
    const uint32_t* cmap = (const uint32_t*)px_map;
    for (int y = 0; y < h; y++) {
        int fy = area->y1 + y;
        if (fy < 0 || fy >= s.height) continue;
        for (int x = 0; x < w; x++) {
            int fx = area->x1 + x;
            if (fx < 0 || fx >= s.width) continue;
            uint32_t c = cmap[(size_t)y * (size_t)w + (size_t)x];  // 0xAARRGGBB little-endian
            s.setpx(fx, fy, c);
        }
    }
    lv_display_flush_ready(disp);
}


static uint32_t keycode_to_lv(int kc, char ascii) {
    switch (kc) {
        case KEY_UP: return LV_KEY_UP;
        case KEY_DOWN: return LV_KEY_DOWN;
        case KEY_LEFT: return LV_KEY_LEFT;
        case KEY_RIGHT: return LV_KEY_RIGHT;
        case KEY_ENTER: return LV_KEY_ENTER;
        case KEY_ESC: return LV_KEY_ESC;
        case KEY_BACKSPACE: return LV_KEY_BACKSPACE;
        case KEY_DEL: return LV_KEY_DEL;
        case KEY_TAB: return LV_KEY_NEXT;
        case KEY_HOME: return LV_KEY_HOME;
        case KEY_END: return LV_KEY_END;
        case KEY_PGUP: return LV_KEY_PREV;
        case KEY_PGDN: return LV_KEY_NEXT;
        default: break;
    }
    if (ascii == '\r' || ascii == '\n') return LV_KEY_ENTER;
    if (ascii == 27) return LV_KEY_ESC;
    if (ascii >= 32 && ascii < 127) return (uint32_t)ascii;
    return 0;
}

static void lvgl_key_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    if (s_lv_key_n) {
        data->key = s_lv_keys[0];
        data->state = LV_INDEV_STATE_PRESSED;
        for (uint8_t i = 0; i + 1 < s_lv_key_n; i++) s_lv_keys[i] = s_lv_keys[i + 1];
        s_lv_key_n--;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
void lvgl_key_push(int keycode, char ascii) {
    uint32_t k = keycode_to_lv(keycode, ascii);
    if (k && s_lv_key_n < 32) s_lv_keys[s_lv_key_n++] = k;
}

lv_group_t* lvgl_kb_group() {
    return s_kb_group;
}

static void lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    data->point.x = s_mx;
    data->point.y = s_my;
    data->state = s_btn ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    static int s_lvdbg = 0;
    if (s_lvdbg < 60 && (data->state == LV_INDEV_STATE_PRESSED || s_mx != 10 || s_my != 10)) {
        s_lvdbg++; klogf("lvgl in=%d,%d st=%d\n", s_mx, s_my, (int)data->state);
    }
}

void desktop_redraw() {
    Screen* scr = platform_screen();
    if (!scr) return;
    Surface fb;
    fb.addr = scr->addr;
    fb.width = scr->width;
    fb.height = scr->height;
    fb.pitch = scr->pitch;
    desktop_paint(fb);
    platform_present();
}

// ---- icon events ----
static void on_icon_click(lv_event_t* e) {
    // keyboard Enter on a focused icon must not launch apps: only a real
    // pointer click should. Otherwise pressing Enter inside an open window
    // accidentally launches the first desktop icon.
    lv_indev_t* indev = lv_event_get_indev(e);
    if (indev == s_kb_indev) return;
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i >= 0 && i < MAX_ICONS && s_icons[i].used && !s_icons[i].deleted) app_launch(s_icons[i].app);
}

static void on_icon_release(lv_event_t* e) {
    // snap dragged icon back to the grid, never overlapping
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= MAX_ICONS || !s_icons[i].used) return;
    DesktopIcon& ic = s_icons[i];
    lv_obj_t* obj = s_icon_objs[i];
    lv_obj_update_layout(obj);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    ic.x = a.x1;
    ic.y = a.y1;
    int col = (ic.x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
    int row = (ic.y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    int best_c = col, best_r = row, best_d2 = 0x7FFFFFFF;
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            bool taken = false;
            for (int k = 0; k < MAX_ICONS; k++) {
                if (k == i || !s_icons[k].used || s_icons[k].deleted) continue;
                DesktopIcon& o = s_icons[k];
                int oc = (o.x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
                int orw = (o.y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
                if (oc == c && orw == r) { taken = true; break; }
            }
            if (!taken) {
                int d2 = (c - col) * (c - col) + (r - row) * (r - row);
                if (d2 < best_d2) { best_d2 = d2; best_c = c; best_r = r; }
            }
        }
    }
    ic.x = 14 + best_c * (ICON_W + 8);
    ic.y = 14 + best_r * (ICON_H + 10);
    lv_obj_set_pos(obj, ic.x, ic.y);
}

// ---- start menu ----
static void start_menu_toggle() {
    if (!s_start_menu) return;
    if (lv_obj_has_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = true;
    } else {
        lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = false;
    }
}

// Exposed to the window manager so open windows draw around the menu and
// clicks inside it are not captured by a window underneath.
bool start_menu_visible() {
    return s_start_open && s_start_menu &&
           !lv_obj_has_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
}
void start_menu_rect(int* x1, int* y1, int* x2, int* y2) {
    *x1 = *y1 = *x2 = *y2 = 0;
    if (!start_menu_visible()) return;
    lv_area_t a;
    lv_obj_get_coords(s_start_menu, &a);
    *x1 = a.x1; *y1 = a.y1; *x2 = a.x2; *y2 = a.y2;
}

static void on_start_click(lv_event_t* e) {
    (void)e;
    start_menu_toggle();
}

static void on_menu_launch(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
    s_start_open = false;
    if (idx >= 0 && idx < s_menu_count) app_launch(s_menu_apps[idx]);
}

// ---- desktop context menu ----
// icon context menu: 0=Open 1=Rename 2=Delete shortcut 3=Properties
// desktop context menu: 0=Refresh 1=Arrange 2=New shortcut 3=Wallpaper
//                      4=Settings 5=Terminal 6=Lock screen
// Modal dialogs: rename / properties / new-shortcut picker.

// forward declarations (handlers -> dialogs / actions defined below)
static void rmenu_dialog_close();
static void rmenu_rename_icon(int i);
static void rmenu_props_icon(int i);
static void rmenu_new_shortcut();
static void arrange_icons();
static lv_obj_t* make_icon(lv_obj_t* scr, int i, int x, int y);

static void rmenu_place(lv_obj_t* m, int mw, int mh, int x, int y) {
    int W = platform_screen()->width;
    int H = platform_screen()->height;
    int px = x + 6, py = y + 6;               // open below-right of the cursor
    if (px + mw > W) px = x - mw - 2;         // flip left near the right edge
    if (py + mh > H - TASKBAR_H) py = y - mh - 2;  // flip up near the bottom (above taskbar)
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    lv_obj_set_pos(m, px, py);
}

static void rmenu_hide_all() {
    if (s_rmenu) lv_obj_add_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
    if (s_rmenu_desk) lv_obj_add_flag(s_rmenu_desk, LV_OBJ_FLAG_HIDDEN);
    s_rmenu_open = false;
    s_rmenu_desk_open = false;
}

static void rmenu_show_icon(int i, int x, int y) {
    if (!s_rmenu) return;
    s_rmenu_icon = i;
    rmenu_hide_all();                         // closes the desktop menu as well
    if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
    s_start_open = false;
    lv_obj_remove_flag(s_rmenu, LV_OBJ_FLAG_HIDDEN);
    rmenu_place(s_rmenu, s_rmenu_mw, s_rmenu_mh, x, y);
    s_rmenu_open = true;
}

static void rmenu_show_desk(int x, int y) {
    if (!s_rmenu_desk) return;
    rmenu_hide_all();
    if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
    s_start_open = false;
    lv_obj_remove_flag(s_rmenu_desk, LV_OBJ_FLAG_HIDDEN);
    rmenu_place(s_rmenu_desk, s_rmenu_desk_mw, s_rmenu_desk_mh, x, y);
    s_rmenu_desk_open = true;
}

static void on_rmenu_item(lv_event_t* e) {
    int item = (int)(intptr_t)lv_event_get_user_data(e);
    int i = s_rmenu_icon;
    rmenu_hide_all();
    bool ok = (i >= 0 && i < MAX_ICONS && s_icons[i].used && !s_icons[i].deleted);
    switch (item) {
    case 0:  if (ok) app_launch(s_icons[i].app); break;
    case 1:  if (ok) rmenu_rename_icon(i); break;
    case 2:  if (ok) {
                 s_icons[i].deleted = true;   // shortcut only; app stays installed
                 if (s_icon_objs[i]) lv_obj_add_flag(s_icon_objs[i], LV_OBJ_FLAG_HIDDEN);
                 s_dirty = true;
             } break;
    case 3:  if (ok) rmenu_props_icon(i); break;
    default: break;
    }
}

static void on_rmenu_desk_item(lv_event_t* e) {
    int item = (int)(intptr_t)lv_event_get_user_data(e);
    rmenu_hide_all();
    switch (item) {
    case 0:  s_dirty = true; break;                                  // refresh
    case 1:  arrange_icons(); break;                                 // snap to grid
    case 2:  rmenu_new_shortcut(); break;                            // pick an app
    case 3:  app_launch(APP_WALLPAPER); break;
    case 4:  app_launch(APP_SETTINGS); break;
    case 5:  app_launch(APP_TERMINAL); break;
    case 6:  lock_screen(); break;
    default: break;
    }
}

static lv_obj_t* rmenu_make_panel(lv_obj_t* scr, int* mw, int* mh, int n_items) {
    int w = 158;
    int h = 4 + n_items * 24 + 4;
    lv_obj_t* m = lv_obj_create(scr);
    lv_obj_set_size(m, w, h);
    lv_obj_set_pos(m, 40, 40);
    lv_obj_set_style_bg_color(m, lv_color_hex(0xF5F5F3), 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m, 6, 0);
    lv_obj_set_style_border_width(m, 1, 0);
    lv_obj_set_style_border_color(m, lv_color_hex(0xA0A8B0), 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_shadow_width(m, 14, 0);
    lv_obj_set_style_shadow_opa(m, LV_OPA_20, 0);
    lv_obj_set_style_shadow_ofs_y(m, 3, 0);
    lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(m);
    *mw = w; *mh = h;
    return m;
}

static lv_obj_t* rmenu_make_item(lv_obj_t* parent, const char* text, int idx, lv_event_cb_t cb) {
    lv_obj_t* it = lv_button_create(parent);
    lv_obj_set_size(it, 152, 22);
    lv_obj_set_pos(it, 3, 4 + idx * 24);
    lv_obj_set_style_bg_color(it, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(it, 4, 0);
    lv_obj_set_style_bg_color(it, lv_color_hex(0xDFE7F2), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(it, lv_color_hex(0xDFE7F2), LV_STATE_FOCUSED);
    lv_obj_t* l = lv_label_create(it);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0x22262A), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_add_event_cb(it, cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx);
    return it;
}

// ---- modal dialogs (rename / properties / new shortcut) ----
struct RmenuDialog {
    lv_obj_t* overlay;     // full-screen dim + click-to-close catcher
    lv_obj_t* panel;
    lv_obj_t* ta;          // textarea (rename dialog) or 0
    int icon;              // target icon index
};
static RmenuDialog* s_dialog = 0;

static void on_dialog_overlay_click(lv_event_t* e) {
    // only close when the click hit the overlay itself, not a panel child
    if (lv_event_get_target(e) != lv_event_get_user_data(e)) return;
    rmenu_dialog_close();
}

static void rmenu_dialog_close() {
    RmenuDialog* d = s_dialog;
    if (!d) return;
    if (d->ta) {
        lv_group_t* grp = lvgl_kb_group();
        if (grp) lv_group_remove_obj(d->ta);
    }
    if (d->overlay) lv_obj_delete(d->overlay);
    delete d;
    s_dialog = 0;
}

static lv_obj_t* rmenu_dialog_overlay() {
    int W = platform_screen()->width, H = platform_screen()->height;
    lv_obj_t* o = lv_obj_create(lv_screen_active());
    lv_obj_set_size(o, W, H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_30, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o, on_dialog_overlay_click, LV_EVENT_CLICKED, o);
    lv_obj_move_foreground(o);
    return o;
}

static lv_obj_t* rmenu_dialog_panel(lv_obj_t* parent, int w, int h) {
    lv_obj_t* p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_align(p, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(0xF5F5F3), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, 8, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, lv_color_hex(0x9AA5BF), 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_shadow_width(p, 18, 0);
    lv_obj_set_style_shadow_opa(p, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(p, 4, 0);
    return p;
}

static lv_obj_t* rmenu_dialog_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, lv_event_cb_t cb, bool accent) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, accent ? lv_color_hex(0x3D4B66) : lv_color_hex(0xE4E4E0), 0);
    lv_obj_set_style_radius(b, 4, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, accent ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x22262A), 0);
    lv_obj_center(l);
    return b;
}

static void on_rename_ok(lv_event_t* e) {
    (void)e;
    RmenuDialog* d = s_dialog;
    if (!d) return;
    int i = d->icon;
    if (i >= 0 && i < MAX_ICONS && s_icons[i].used && d->ta) {
        const char* t = lv_textarea_get_text(d->ta);
        if (t[0]) {
            strncpy(s_icons[i].label, t, sizeof(s_icons[i].label) - 1);
            s_icons[i].label[sizeof(s_icons[i].label) - 1] = 0;
            if (s_icon_labels[i]) lv_label_set_text(s_icon_labels[i], s_icons[i].label);
            s_dirty = true;
        }
    }
    rmenu_dialog_close();
}

static void on_dialog_cancel(lv_event_t* e) {
    (void)e;
    rmenu_dialog_close();
}

static void rmenu_rename_icon(int i) {
    if (s_dialog || i < 0 || i >= MAX_ICONS || !s_icons[i].used) return;
    RmenuDialog* d = new RmenuDialog();
    d->icon = i;
    d->ta = 0;
    d->overlay = rmenu_dialog_overlay();
    d->panel = rmenu_dialog_panel(d->overlay, 336, 132);
    lv_obj_t* title = lv_label_create(d->panel);
    lv_label_set_text(title, T("重命名快捷方式", "Rename Shortcut"));
    lv_obj_set_style_text_color(title, lv_color_hex(0x22262A), 0);
    lv_obj_set_pos(title, 16, 12);
    d->ta = lv_textarea_create(d->panel);
    lv_obj_set_size(d->ta, 304, 30);
    lv_obj_set_pos(d->ta, 16, 38);
    lv_obj_set_style_bg_color(d->ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(d->ta, lv_color_hex(0x9AA5BF), 0);
    lv_textarea_set_one_line(d->ta, true);
    lv_textarea_set_text(d->ta, s_icons[i].label);
    lv_obj_add_event_cb(d->ta, on_rename_ok, LV_EVENT_READY, 0);
    rmenu_dialog_button(d->panel, T("确定", "OK"), 128, 88, 88, 26, on_rename_ok, true);
    rmenu_dialog_button(d->panel, T("取消", "Cancel"), 224, 88, 88, 26, on_dialog_cancel, false);
    lv_group_t* grp = lvgl_kb_group();
    if (grp) {
        lv_group_add_obj(grp, d->ta);
        lv_group_focus_obj(d->ta);
    }
    s_dialog = d;
}

static void rmenu_props_icon(int i) {
    if (s_dialog || i < 0 || i >= MAX_ICONS || !s_icons[i].used) return;
    RmenuDialog* d = new RmenuDialog();
    d->icon = i;
    d->ta = 0;
    d->overlay = rmenu_dialog_overlay();
    d->panel = rmenu_dialog_panel(d->overlay, 340, 168);
    lv_obj_t* title = lv_label_create(d->panel);
    lv_label_set_text(title, T("属性", "Properties"));
    lv_obj_set_style_text_color(title, lv_color_hex(0x22262A), 0);
    lv_obj_set_pos(title, 16, 12);
    DesktopIcon& ic = s_icons[i];
    int col = (ic.x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
    int row = (ic.y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    char buf[160];
    ksprintf(buf, sizeof(buf), T("名称：%s", "Name: %s"), ic.label);
    lv_obj_t* l1 = lv_label_create(d->panel);
    lv_label_set_text(l1, buf);
    lv_obj_set_style_text_color(l1, lv_color_hex(0x33373C), 0);
    lv_obj_set_pos(l1, 16, 42);
    ksprintf(buf, sizeof(buf), T("应用：%s（ID %d）", "App: %s (ID %d)"), app_name(ic.app), ic.app);
    lv_obj_t* l2 = lv_label_create(d->panel);
    lv_label_set_text(l2, buf);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x33373C), 0);
    lv_obj_set_pos(l2, 16, 66);
    ksprintf(buf, sizeof(buf), T("位置：第 %d 列，第 %d 行", "Grid: col %d, row %d"), col + 1, row + 1);
    lv_obj_t* l3 = lv_label_create(d->panel);
    lv_label_set_text(l3, buf);
    lv_obj_set_style_text_color(l3, lv_color_hex(0x33373C), 0);
    lv_obj_set_pos(l3, 16, 90);
    rmenu_dialog_button(d->panel, T("确定", "OK"), 126, 122, 88, 26, on_dialog_cancel, true);
    s_dialog = d;
}

static bool icon_free_cell(int* cx, int* cy) {
    for (int r = 0; r < GRID_ROWS; r++) {
        for (int c = 0; c < GRID_COLS; c++) {
            bool taken = false;
            for (int k = 0; k < MAX_ICONS; k++) {
                if (!s_icons[k].used || s_icons[k].deleted) continue;
                int kc = (s_icons[k].x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
                int kr = (s_icons[k].y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
                if (kc == c && kr == r) { taken = true; break; }
            }
            if (!taken) { *cx = c; *cy = r; return true; }
        }
    }
    return false;
}

static void arrange_icons() {
    int n = 0;
    for (int i = 0; i < MAX_ICONS; i++) {
        if (!s_icons[i].used || s_icons[i].deleted) continue;
        int row = n / GRID_COLS;
        if (row >= GRID_ROWS) {         // grid full: keep the slot, hide the tile
            if (s_icon_objs[i]) lv_obj_add_flag(s_icon_objs[i], LV_OBJ_FLAG_HIDDEN);
            n++;
            continue;
        }
        s_icons[i].x = 14 + (n % GRID_COLS) * (ICON_W + 8);
        s_icons[i].y = 14 + row * (ICON_H + 10);
        if (s_icon_objs[i]) {
            lv_obj_remove_flag(s_icon_objs[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_icon_objs[i], s_icons[i].x, s_icons[i].y);
        }
        n++;
    }
    s_dirty = true;
}

static void on_pick_app(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int app = (idx >= 0 && idx < s_menu_count) ? s_menu_apps[idx] : -1;
    if (app >= 0) {
        int slot = -1;
        for (int i = 0; i < MAX_ICONS; i++) { if (!s_icons[i].used) { slot = i; break; } }
        if (slot >= 0) {
            int cx = 0, cy = 0;
            icon_free_cell(&cx, &cy);
            DesktopIcon& ic = s_icons[slot];
            ic.used = true;
            ic.deleted = false;
            ic.app = app;
            ic.x = 14 + cx * (ICON_W + 8);
            ic.y = 14 + cy * (ICON_H + 10);
            const char* lbl = icon_label(app);
            strncpy(ic.label, lbl, sizeof(ic.label) - 1);
            ic.label[sizeof(ic.label) - 1] = 0;
            s_icon_objs[slot] = make_icon(lv_screen_active(), slot, ic.x, ic.y);
            s_dirty = true;
        } else {
            klogf("rmenu: no free icon slot for new shortcut\n");
        }
    }
    rmenu_dialog_close();
}

static void rmenu_new_shortcut() {
    if (s_dialog || s_menu_count <= 0) return;
    RmenuDialog* d = new RmenuDialog();
    d->icon = -1;
    d->ta = 0;
    d->overlay = rmenu_dialog_overlay();
    int list_rows = s_menu_count < 8 ? s_menu_count : 8;
    int ph = 40 + list_rows * 26 + 10;
    d->panel = rmenu_dialog_panel(d->overlay, 240, ph);
    lv_obj_t* title = lv_label_create(d->panel);
    lv_label_set_text(title, T("选择应用创建快捷方式", "Pick an app"));
    lv_obj_set_style_text_color(title, lv_color_hex(0x22262A), 0);
    lv_obj_set_pos(title, 16, 10);
    lv_obj_t* list = lv_obj_create(d->panel);
    lv_obj_set_size(list, 224, list_rows * 26);
    lv_obj_set_pos(list, 8, 34);
    lv_obj_set_style_bg_color(list, lv_color_hex(0xF5F5F3), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    for (int i = 0; i < s_menu_count; i++) {
        lv_obj_t* b = lv_button_create(list);
        lv_obj_set_size(b, 220, 24);
        lv_obj_set_pos(b, 2, i * 26);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_add_event_cb(b, on_pick_app, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, app_name(s_menu_apps[i]));
        lv_obj_set_style_text_color(l, lv_color_hex(0x22262A), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
    }
    s_dialog = d;
}

// ---- wallpaper ----
static void build_wallpaper(lv_obj_t* scr, int W, int H) {
    uint32_t top = 0x00345C86, bottom = 0x0088B7D8, base = color::CREAM;
    wallpaper_colors(g_settings.wallpaper, &top, &bottom, &base);
    lv_obj_set_style_bg_color(scr, lv_color_hex(base & 0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    // top gradient band (wallpaper)
    lv_obj_t* gband = lv_obj_create(scr);
    lv_obj_set_size(gband, W, H - TASKBAR_H);
    lv_obj_set_pos(gband, 0, 0);
    lv_obj_set_style_bg_color(gband, lv_color_hex(top & 0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_color(gband, lv_color_hex(bottom & 0xFFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(gband, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_stop(gband, 235, 0);
    lv_obj_set_style_bg_opa(gband, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gband, 0, 0);
    lv_obj_set_style_radius(gband, 0, 0);
    // title watermark
    lv_obj_t* wm = lv_label_create(scr);
    lv_label_set_text(wm, "nefuOS 0.1  (LVGL GUI)");
    lv_obj_set_style_text_color(wm, lv_color_hex(0x60FFFFFF), 0);
    lv_obj_align(wm, LV_ALIGN_BOTTOM_RIGHT, -14, -TASKBAR_H - 16);
}

// ---- icon tile color helper ----
static uint32_t icon_color(int app) {
    switch (app) {
    case APP_FILEMGR:    return 0xE6B94A;
    case APP_TERMINAL:   return 0x3B4654;
    case APP_CALC:       return 0x3A6EA5;
    case APP_TEXTVIEW:   return 0xE8E6DF;
    case APP_WIKI:       return 0x2A7A6C;
    case APP_SETTINGS:   return 0x666C74;
    case APP_STORE:      return 0xE67E22;
    case APP_IMAGEVIEWER:return 0xF5F1E8;
    case APP_MUSIC:      return 0x14181E;
    case APP_VIDEOPLAYER:return 0xE74C3C;
    case APP_MONITOR:    return 0x14181E;
    case APP_BROWSER:    return 0x5A9BD4;
    case APP_NETCFG:     return 0x3FA45A;
    case APP_NEFUD:      return 0xE67E22;
    case APP_CALENDAR:   return 0xE74C3C;
    case APP_DISKUSAGE:  return 0x9B59B6;
    case APP_PASSGEN:    return 0x1ABC9C;
    case APP_STICKY:     return 0xF1C40F;
    case APP_EMAIL:      return 0x3498DB;
    case APP_GAMECENTER: return 0xE74C3C;
    case APP_WIDGETS:    return 0x2ECC71;
    case APP_CLOCK_TIMER:      return 0x9B59B6;
    case APP_CONVERTER:   return 0xE67E22;
    case APP_TETRIS:      return 0x3498DB;
    case APP_GAME2048:    return 0xE67E22;
    case APP_SUDOKU:      return 0x27AE60;
    case APP_MEMORYMATCH: return 0xE74C3C;
    case APP_HEXEDIT:     return 0x2C3E50;
    case APP_JSONVIEW:    return 0x8E44AD;
    case APP_FINDFILES:   return 0x2980B9;
    case APP_POMODORO:    return 0xD35400;
    case APP_STOPWATCH:   return 0x16A085;
    case APP_WORDCOUNT:   return 0x7F8C8D;
    case APP_ALGOVIZ:     return 0xE67E22;
    case APP_GFXLAB:      return 0x8E44AD;
    case APP_SIMLAB:      return 0x16A085;
    case APP_TEXTTOOL:    return 0x1ABC9C;
    case APP_CRYPTOLAB:   return 0x27AE60;
    case APP_COMPRESSTOOL: return 0xE67E22;
    case APP_SERIALAB:    return 0x2980B9;
    case APP_AUDIOLAB:    return 0x8E44AD;
    case APP_GFX3DVIEW:   return 0x66CCFF;
    case APP_MATHTOOL:    return 0x2E86C1;
    case APP_WIDGETGALLERY: return 0x16A085;
    case APP_DBMANAGER:   return 0x8E44AD;
    case APP_BREAKOUT:    return 0xE74C3C;
    case APP_PONG:        return 0x3498DB;
    case APP_FLAPPY:      return 0xF1C40F;
    case APP_SPACEINV:    return 0x2C3E50;
    case APP_PACMAN:      return 0xF39C12;
    case APP_TICTACTOE:   return 0x1ABC9C;
    case APP_CONNECT4:    return 0x2980B9;
    case APP_MINESWEEPER2: return 0x27AE60;
    case APP_LIFE:        return 0x9B59B6;
    case APP_MLLAB:       return 0xE74C3C;
    case APP_REPL:        return 0x2C3E50;
    case APP_SYSMON:      return 0x1ABC9C;
    case APP_NETLAB:      return 0x2980B9;
    case APP_FSVIEW:      return 0x89B4FA;
    case APP_RAYVIEW:     return 0x3498DB;
    case APP_COMPILERLAB: return 0xE67E22;
    case APP_GEDEMO: return 0x27AE60;
    case APP_DEEPVIZ: return 0x9B59B6;
    case APP_WEATHER:    return 0x3498DB;
    case APP_HELP:       return 0x9B59B6;
    case APP_DICTIONARY: return 0xE67E22;
    default:             return 0x8899AA;
    }
}

static lv_obj_t* make_icon(lv_obj_t* scr, int i, int x, int y) {
    DesktopIcon& ic = s_icons[i];
    s_icon_color[i] = icon_color(ic.app);
    lv_obj_t* card = lv_button_create(scr);
    lv_obj_set_size(card, ICON_W, ICON_H);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E222C), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x40FFFFFF), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    // LVGL v9 objects are draggable by default (press + move)
    // app tile
    lv_obj_t* tile = lv_obj_create(card);
    lv_obj_set_size(tile, TILE, TILE);
    lv_obj_align(tile, LV_ALIGN_TOP_MID, 0, 2);
    // tile must not capture the click: it has no CLICKED handler, and an
    // lv_obj is clickable+scrollable by default, so a press on the icon art
    // would never reach the parent card (no event bubbling). Make it inert so
    // LVGL's hit-test falls through to the card, which owns on_icon_click.
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(tile, lv_color_hex(s_icon_color[i]), 0);
    lv_obj_set_style_radius(tile, 6, 0);
    lv_obj_set_style_border_width(tile, 1, 0);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x40000000), 0);
    lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x88FFFFFF), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_stop(tile, 60, 0);
    // label (kept in ic.label so Rename can edit it)
    const char* lbl = icon_label(ic.app);
    strncpy(ic.label, lbl, sizeof(ic.label) - 1);
    ic.label[sizeof(ic.label) - 1] = 0;
    lv_obj_t* lab = lv_label_create(card);
    lv_label_set_text(lab, ic.label);
    lv_obj_set_style_text_color(lab, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lab, &lv_font_montserrat_14, 0);
    lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -4);
    s_icon_labels[i] = lab;
    lv_obj_add_event_cb(card, on_icon_click, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_add_event_cb(card, on_icon_release, LV_EVENT_RELEASED, (void*)(intptr_t)i);
    return card;
}

// start menu entries: built-in apps + settings + store + installed store apps
// + every desktop shortcut (games, tools, ...) so no app becomes unreachable
// when the desktop grid hides rows that no longer fit above the taskbar.
static int build_menu(int* list) {
    int n = 0;
    for (int i = 0; i < APP_BUILTIN_COUNT && n < 30; i++) list[n++] = i;
    if (n < 30) list[n++] = APP_SETTINGS;
    if (n < 30) list[n++] = APP_STORE;
    int ids[8];
    int cnt = app_installed_list(ids, 8);
    for (int i = 0; i < cnt && n < 30; i++) list[n++] = ids[i];
    for (int i = 0; i < MAX_ICONS && n < 30; i++) {
        if (!s_icons[i].used || s_icons[i].deleted) continue;
        int a = s_icons[i].app;
        bool dup = false;
        for (int k = 0; k < n; k++) if (list[k] == a) { dup = true; break; }
        if (!dup) list[n++] = a;
    }
    return n;
}

// ---- taskbar + start menu ----
static void build_taskbar(lv_obj_t* scr, int W, int H) {
    lv_obj_t* bar = lv_obj_create(scr);
    lv_obj_set_size(bar, W, TASKBAR_H);
    lv_obj_set_pos(bar, 0, H - TASKBAR_H);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x262A31), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x4F5A66), 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_move_foreground(bar);

    // start button
    uint32_t accent = accent_color(g_settings.accent);
    lv_obj_t* sb = lv_button_create(bar);
    lv_obj_set_size(sb, 64, 24);
    lv_obj_set_pos(sb, 4, 3);
    lv_obj_set_style_bg_color(sb, lv_color_hex(accent & 0xFFFFFF), 0);
    lv_obj_set_style_radius(sb, 4, 0);
    lv_obj_add_event_cb(sb, on_start_click, LV_EVENT_CLICKED, 0);
    lv_obj_t* sl = lv_label_create(sb);
    lv_label_set_text(sl, "Start");
    lv_obj_set_style_text_color(sl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(sl);

    // clock
    s_clock_label = lv_label_create(bar);
    lv_label_set_text(s_clock_label, "--:--");
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_clock_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_clock_label, LV_ALIGN_RIGHT_MID, -10, 0);
}

static void build_start_menu(lv_obj_t* scr, int W, int H) {
    s_menu_count = build_menu(s_menu_apps);
    int rows = s_menu_count;
    int mw = 196;
    int mh = rows * 26 + 10 + 32;
    int mx = 4, my = H - TASKBAR_H - mh;
    if (my < 0) {
        // Too many apps to fit above the taskbar: cap the height and let
        // LVGL scroll the item list (the Power Off row stays reachable).
        mh = H - TASKBAR_H - 8;
        my = H - TASKBAR_H - mh;
    }
    s_start_menu = lv_obj_create(scr);
    lv_obj_set_size(s_start_menu, mw, mh);
    lv_obj_set_pos(s_start_menu, mx, my);
    lv_obj_set_style_bg_color(s_start_menu, lv_color_hex(0xF2F2F0), 0);
    lv_obj_set_style_bg_opa(s_start_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_start_menu, 8, 0);
    lv_obj_set_style_border_width(s_start_menu, 1, 0);
    lv_obj_set_style_border_color(s_start_menu, lv_color_hex(0xA0A8B0), 0);
    lv_obj_set_style_pad_all(s_start_menu, 0, 0);
    lv_obj_set_scrollbar_mode(s_start_menu, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_start_menu);

    int y = 4;
    for (int i = 0; i < rows; i++) {
        lv_obj_t* it = lv_button_create(s_start_menu);
        lv_obj_set_size(it, mw - 6, 24);
        lv_obj_set_pos(it, 3, y);
        lv_obj_set_style_bg_color(it, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(it, 4, 0);
        lv_obj_add_event_cb(it, on_menu_launch, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* l = lv_label_create(it);
        lv_label_set_text(l, app_name(s_menu_apps[i]));
        lv_obj_set_style_text_color(l, lv_color_hex(0x22262A), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);
        y += 26;
    }
    lv_obj_t* sep = lv_obj_create(s_start_menu);
    lv_obj_set_size(sep, mw - 16, 1);
    lv_obj_set_pos(sep, 8, y + 4);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0xC0C4C8), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    y += 10;
    lv_obj_t* po = lv_button_create(s_start_menu);
    lv_obj_set_size(po, mw - 6, 24);
    lv_obj_set_pos(po, 3, y);
    lv_obj_set_style_bg_color(po, lv_color_hex(0xF5DEDC), 0);
    lv_obj_set_style_radius(po, 4, 0);
    lv_obj_add_event_cb(po, on_menu_launch, LV_EVENT_CLICKED, (void*)(intptr_t)(-1));
    lv_obj_t* pl = lv_label_create(po);
    lv_label_set_text(pl, "Power Off");
    lv_obj_set_style_text_color(pl, lv_color_hex(0xC22A2A), 0);
    lv_obj_align(pl, LV_ALIGN_LEFT_MID, 10, 0);
}

static void build_rmenu(lv_obj_t* scr) {
    // icon shortcut context menu (4 items)
    s_rmenu = rmenu_make_panel(scr, &s_rmenu_mw, &s_rmenu_mh, 4);
    const char* items[4] = {
        T("打开", "Open"),
        T("重命名", "Rename"),
        T("删除快捷方式", "Delete Shortcut"),
        T("属性", "Properties"),
    };
    for (int i = 0; i < 4; i++) s_rmenu_items[i] = rmenu_make_item(s_rmenu, items[i], i, on_rmenu_item);

    // blank-desktop context menu (7 items)
    s_rmenu_desk = rmenu_make_panel(scr, &s_rmenu_desk_mw, &s_rmenu_desk_mh, 7);
    const char* desk_items[7] = {
        T("刷新桌面", "Refresh"),
        T("排列图标", "Arrange Icons"),
        T("新建快捷方式", "New Shortcut"),
        T("更改壁纸", "Wallpaper"),
        T("设置", "Settings"),
        T("打开终端", "Terminal"),
        T("锁定屏幕", "Lock Screen"),
    };
    for (int i = 0; i < 7; i++) s_rmenu_desk_items[i] = rmenu_make_item(s_rmenu_desk, desk_items[i], i, on_rmenu_desk_item);
}

static void on_clock_tick(lv_timer_t* t) {
    (void)t;
    if (!s_clock_label) return;
    uint32_t sec = platform_seconds_of_day();
    char buf[16];
    ksprintf(buf, sizeof(buf), "%02u:%02u", (sec / 3600) % 24, (sec / 60) % 60);
    lv_label_set_text(s_clock_label, buf);
}

// ---- public desktop API ----
static void desktop_lvgl_init(Surface& fb);

void desktop_init() {
    s_start_open = false;
    s_rmenu_open = false;
    s_initialized = false;
    s_dirty = true;
    s_last_sig = 0;
    Surface fb = screen_surface();
    desktop_lvgl_init(fb);
}

// Window overlays change the desktop pixels underneath (window moved / resized
// / opened / closed). Hash visible windows to detect exactly that; only then
// force a full LVGL redraw. Otherwise LVGL redraws only invalid regions, so
// an idle desktop costs almost no CPU (no busy-loop, no CPU saturation).
static uint32_t window_signature() {
    uint32_t sig = 0;
    List<Window*>& ws = g_wm->windows();
    for (int i = 0; i < ws.size(); i++) {
        Window* w = ws[i];
        if (!w->visible || w->closed) continue;
        sig = sig * 131u + (uint32_t)w->x * 7u + (uint32_t)w->y * 13u +
              (uint32_t)w->w * 17u + (uint32_t)w->h * 19u +
              (uint32_t)w->title.len() + (w->minimized ? 5u : 0u);
    }
    return sig;
}

// One-time LVGL initialisation. Called early from desktop_init() so that
// the host "--app N" mode (which never shows the lock screen before the
// app window is created) has a working LVGL allocator; desktop_paint()
// just skips this block once s_initialized is set.
static void desktop_lvgl_init(Surface& fb) {
    if (s_initialized) return;
    int W = fb.width, H = fb.height;
    lv_init();
    s_fb_buf = (lv_color_t*)kalloc((size_t)W * 24u * 4u);
    if (!s_fb_buf) { s_initialized = true; return; }
    s_disp = lv_display_create(W, H);
    lv_display_set_flush_cb(s_disp, lvgl_flush_cb);
    lv_display_set_buffers(s_disp, s_fb_buf, NULL,
                           (size_t)W * 24u * 4u, LV_DISPLAY_RENDER_MODE_PARTIAL);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, lvgl_read_cb);
    lv_indev_set_display(s_indev, s_disp);

    // keyboard input (keypad indev + shared group for text widgets)
    s_kb_indev = lv_indev_create();
    lv_indev_set_type(s_kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(s_kb_indev, lvgl_key_read_cb);
    lv_indev_set_display(s_kb_indev, s_disp);
    s_kb_group = lv_group_create();
    lv_indev_set_group(s_kb_indev, s_kb_group);
    lv_group_set_default(s_kb_group);

    lv_obj_t* scr = lv_screen_active();
    build_wallpaper(scr, W, H);
    // Keep each icon at its configured grid position; rows that no longer fit
    // above the taskbar (GRID_ROWS) are not drawn at all instead of being put
    // below the taskbar / off-screen ("apps at the bottom"). Those shortcuts
    // stay available via the Start menu (build_menu includes every icon app).
    for (int i = 0; i < MAX_ICONS; i++) {
        if (!s_icons[i].used || s_icons[i].deleted) continue;
        int row = (s_icons[i].y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
        if (row >= GRID_ROWS) { s_icon_objs[i] = 0; continue; }
        s_icon_objs[i] = make_icon(scr, i, s_icons[i].x, s_icons[i].y);
    }
    build_taskbar(scr, W, H);
    build_start_menu(scr, W, H);
    build_rmenu(scr);
    lv_timer_create(on_clock_tick, 1000, NULL);
    s_initialized = true;
    s_dirty = true;
}

void desktop_paint(Surface& fb) {
    desktop_lvgl_init(fb);
    if (!s_initialized) return;
    int W = fb.width, H = fb.height;
    // external overlay changed? then repaint the full desktop
    uint32_t sig = window_signature();
    if (sig != s_last_sig) { s_dirty = true; s_last_sig = sig; }
    if (s_dirty) {
        lv_obj_update_layout(lv_screen_active()); // layout first so coords exist
        lv_obj_invalidate(lv_screen_active());
        s_dirty = false;
    }
    s_surf = &fb;
    lv_tick_inc(16);
    lv_timer_handler();
    lv_refr_now(s_disp);
    s_surf = 0;
}

void desktop_paint_boot(Surface& fb) {
    fb.fill(0x0012141A);
    int W = fb.width, H = fb.height;
    gfx::text_scale(fb, W / 2 - 4 * 8 * 4, H / 2 - 90, "nefuOS", color::WHITE, 0x0012141A, 4);
    gfx::text(fb, W / 2 - 52, H / 2 - 18, "v0.1.0  C++/C  tiny OS", color::BLUE_LT, 0x0012141A);
    int bw = 300, bx = W / 2 - bw / 2, by = H / 2 + 16;
    gfx::rect(fb, bx, by, bw, 12, 0x00404A58);
    int p = (platform_tick_ms() / 40) % (bw - 8);
    gfx::fillrect(fb, bx + 3, by + 3, p, 6, color::BLUE_LT);
    gfx::text(fb, W / 2 - 30, by + 18, "Loading...", color::TEXT2, 0x0012141A);
}

// Lock screen: full-screen wallpaper + UEFI username + password gate. Shown
// between boot splash and the desktop (and on idle lock / suspend).
void desktop_paint_lock(Surface& fb) {
    uint32_t top, bottom, base;
    wallpaper_colors(g_settings.wallpaper_lock, &top, &bottom, &base);
    fb.fill(top);
    int W = fb.width, H = fb.height;
    // bottom shading band so the panel stands out
    for (int y = H - 200; y < H; y += 4) {
        int t = (y - (H - 200)) * 30 / 200;
        gfx::fillrect(fb, 0, y, W, 2,
                      ((t >> 3) << 16) | ((t >> 2) << 8) | (t >> 2) | 0xFF000000);
    }
    // big clock
    uint32_t sec = platform_seconds_of_day();
    char tbuf[24];
    ksprintf(tbuf, sizeof(tbuf), "%02u:%02u", (sec / 3600) % 24, (sec / 60) % 60);
    gfx::text_scale(fb, W / 2 - 9 * 8 * 3, H / 2 - 150, tbuf, color::WHITE, top, 3);
    gfx::text(fb, W / 2 - 40, H / 2 - 104, "nefuOS", color::WHITE, top);
    // login panel
    int px = W / 2 - 190, py = H / 2 - 40;
    gfx::fillrect(fb, px, py, 380, 132, 0xFF161C28);
    gfx::rect(fb, px, py, 380, 132, 0xFF3A4458);
    char line[96];
    ksprintf(line, sizeof(line), "User: %s",
             g_uefi.username[0] ? g_uefi.username : "user");
    gfx::text(fb, px + 16, py + 14, line, color::WHITE, 0xFF161C28);
    gfx::text(fb, px + 16, py + 46, "Password:", color::BLUE_LT, 0xFF161C28);
    int n = nefuos_lock_len();
    if (n > 40) n = 40;
    char stars[48];
    for (int i = 0; i < n; i++) stars[i] = '*';
    stars[n] = 0;
    gfx::text(fb, px + 120, py + 46, stars, color::WHITE, 0xFF161C28);
    gfx::text(fb, px + 16, py + 80, "Enter = unlock   ESC = reboot", color::TEXT2, 0xFF161C28);
    uint32_t now = platform_tick_ms();
    if (nefuos_lock_fail_ms() && now - nefuos_lock_fail_ms() < 1500)
        gfx::text(fb, px + 16, py + 104, "Wrong password - try again", color::RED, 0xFF161C28);
}

// First-boot setup wizard: full-screen account + browser-engine setup.
// Runs once after the boot splash when /var/lib/nefuos/firstboot is absent.
void desktop_paint_setup(Surface& fb) {
    uint32_t top, bottom, base;
    wallpaper_colors(g_settings.wallpaper_boot, &top, &bottom, &base);
    fb.fill(top);
    int W = fb.width, H = fb.height;
    // panel
    int pw = 560, ph = 396, px = W / 2 - pw / 2, py = H / 2 - ph / 2;
    gfx::fillrect(fb, px, py, pw, ph, 0xFF161C28);
    gfx::rect(fb, px, py, pw, ph, 0xFF3A4458);
    gfx::text_scale(fb, px + 24, py + 18, "nefuOS First Boot Setup", color::WHITE, 0xFF161C28, 2);
    gfx::text(fb, px + 24, py + 52, "Create your account and choose the browser engine.",
              color::TEXT2, 0xFF161C28);

    int fy = py + 88;                 // first field top
    int fh = 46, fw = pw - 48;
    const char* labels[4] = {
        "Username", "Password", "Confirm password", "Browser engine"
    };
    int cur = nefuos_setup_field();
    for (int f = 0; f < 4; f++) {
        int yy = fy + f * (fh + 10);
        gfx::text(fb, px + 24, yy, labels[f], color::BLUE_LT, 0xFF161C28);
        int vx = px + 24, vy = yy + 18, vw = fw - 48, vh = 22;
        uint32_t frame = (f == cur) ? color::BLUE : 0xFF3A4458;
        gfx::fillrect(fb, vx, vy, vw, vh, 0xFF0D1117);
        gfx::rect(fb, vx, vy, vw, vh, frame);
        char val[96];
        val[0] = 0;
        if (f == 0) {
            strncpy(val, nefuos_setup_user(), 31);
            val[31] = 0;
        } else if (f == 1 || f == 2) {
            int n = nefuos_setup_pwd_len(f);
            if (n > 40) n = 40;
            for (int i = 0; i < n; i++) val[i] = '*';
            val[n] = 0;
        } else {
            strcpy(val, nefuos_setup_engine() == 1 ? "No Script (HTML only)"
                                                   : "Mini JS (execute <script>)");
        }
        gfx::text(fb, vx + 6, vy + 5, val, color::WHITE, 0xFF0D1117);
        // blinking caret in the active text field
        if (f == cur && f < 3 && ((platform_tick_ms() / 500) & 1)) {
            int clen = (f == 0) ? (int)strlen(nefuos_setup_user()) : nefuos_setup_pwd_len(f);
            int cx = vx + 8 + clen * 8;
            if (cx < vx + vw - 4) gfx::fillrect(fb, cx, vy + 4, 2, vh - 8, color::BLUE_LT);
        }
        // engine selector hint
        if (f == 3) {
            gfx::text(fb, vx + 6 + (int)strlen(val) * 8 + 10, vy + 5,
                      "<  /  >", color::TEXT2, 0xFF0D1117);
        }
    }
    gfx::text(fb, px + 24, fy + 4 * (fh + 10) + 6,
              "Tab = next field   Enter = continue   <-  -> = choose   ESC = finish",
              color::TEXT2, 0xFF161C28);
    uint32_t fm = nefuos_setup_fail_ms();
    if (fm && platform_tick_ms() - fm < 2500) {
        gfx::text(fb, px + 24, fy + 4 * (fh + 10) + 26,
                  "Passwords do not match or are empty - try again",
                  color::RED, 0xFF161C28);
    }
}

bool desktop_handle_mouse(int x, int y, uint8_t buttons) {
    static int s_dhdbg = 0;
    if (s_dhdbg < 20) { s_dhdbg++; klogf("dh m=%d,%d b=%u\n", x, y, (unsigned)buttons); }
    bool pressed = (buttons & 1) != 0;
    bool r_pressed = (buttons & 2) != 0 && (s_last_buttons & 2) == 0;
    s_last_buttons = buttons;
    // Start menu floats above windows (WM clips it out of every window), so
    // clicks inside the open menu belong to the menu, not to any window
    // underneath. Clicks outside the menu (except the Start button itself,
    // which LVGL toggles) close it first and then fall through normally.
    if (s_start_open && s_start_menu) {
        int H_scr = platform_screen()->height;
        bool in_start_btn = (x >= 4 && x <= 68) &&
                            (y >= H_scr - 27 && y <= H_scr - 3);
        if (!in_start_btn) {
            lv_area_t a;
            lv_obj_get_coords(s_start_menu, &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) {
                s_mx = x;
                s_my = y;
                s_btn = pressed;
                return true;   // menu item handlers fire via LVGL
            }
            lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
            s_start_open = false;
        }
    }
    // windows take priority: never feed clicks under a window to LVGL.
    // BUT still update s_mx/s_my/s_btn so the LVGL indev sees the real
    // cursor position: otherwise the LVGL close (X) button in the window
    // header never receives a click and windows cannot be closed by it.
    if (g_wm->hit(x, y)) {
        s_mx = x;
        s_my = y;
        s_btn = pressed;
        return false;
    }
    // idle pointer moves only trigger LVGL-local hover repaints; the full
    // desktop is redrawn by desktop_paint when a window overlay changes.
    if (s_mx != x || s_my != y || s_btn != pressed) {
        s_mx = x;
        s_my = y;
        s_btn = pressed;
    }
    // a modal dialog owns the desktop while open
    if (s_dialog) return true;

    // right click
    if (r_pressed) {
        // right-click on an icon -> icon context menu
        for (int i = 0; i < MAX_ICONS; i++) {
            DesktopIcon& ic = s_icons[i];
            if (!ic.used || ic.deleted) continue;
            if (x >= ic.x && x < ic.x + ICON_W && y >= ic.y && y < ic.y + ICON_H) {
                rmenu_show_icon(i, x, y);
                return true;
            }
        }
        // right-click inside the open icon menu: keep it where it is
        if (s_rmenu_open && s_rmenu) {
            lv_area_t a;
            lv_obj_get_coords(s_rmenu, &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) return true;
        }
        // right-click on blank desktop -> desktop context menu
        rmenu_show_desk(x, y);
        return true;
    }

    // left click
    if (pressed) {
        // clicks inside an open menu are left to LVGL (item handlers fire)
        if (s_rmenu_open && s_rmenu) {
            lv_area_t a;
            lv_obj_get_coords(s_rmenu, &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) return true;
        }
        if (s_rmenu_desk_open && s_rmenu_desk) {
            lv_area_t a;
            lv_obj_get_coords(s_rmenu_desk, &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) return true;
        }
        if (s_start_open && s_start_menu) {
            lv_area_t a;
            lv_obj_get_coords(s_start_menu, &a);
            if (x >= a.x1 && x <= a.x2 && y >= a.y1 && y <= a.y2) return true;
        }
        // click on blank desktop: dismiss all menus
        rmenu_hide_all();
        if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = false;
    }
    return true; // desktop consumes clicks (LVGL widget events fire via timer)
}

bool desktop_handle_key(int keycode, char ascii) {
    // modal dialog: all keys go to LVGL (textarea); ESC cancels the dialog
    if (s_dialog) {
        if (keycode == KEY_ESC) { rmenu_dialog_close(); return true; }
        lvgl_key_push(keycode, ascii);
        return true;
    }
    lvgl_key_push(keycode, ascii);
    // ESC closes an open context menu first
    if ((s_rmenu_open || s_rmenu_desk_open) && keycode == KEY_ESC) {
        rmenu_hide_all();
        return true;
    }
    if (s_start_open && keycode == KEY_ESC) {
        if (s_start_menu) lv_obj_add_flag(s_start_menu, LV_OBJ_FLAG_HIDDEN);
        s_start_open = false;
        return true;
    }
    if (keycode == KEY_F1) { start_menu_toggle(); return true; }
    if (keycode == KEY_F2) { app_launch(APP_TERMINAL); return true; }
    if (keycode == KEY_F3) { app_launch(APP_FILEMGR); return true; }
    if (keycode == KEY_F4) { app_launch(APP_MUSIC); return true; }
    if (keycode == KEY_F5) { app_launch(APP_SETTINGS); return true; }
    if (keycode == KEY_F6) { app_launch(APP_BROWSER); return true; }
    if (keycode == KEY_F7) { app_launch(APP_VIDEOPLAYER); return true; }
    return false;
}

} // namespace nefu
