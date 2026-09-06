// nefuOS 桌面实现：壁纸、图标、任务栏、开始菜单、启动画面
#include "desktop.h"
#include "../apps/apps.h"
#include "../sys/settings.h"
#include "../platform.h"

namespace nefu {

static const int TASKBAR_H = 30;
static const int ICON_W = 96, ICON_H = 92;
static const int TILE = 52;

struct DesktopIcon {
    int x, y;
    const char* label;
    int app;
    bool deleted;          // shortcut removed (app still installed)
    DesktopIcon(int px, int py, const char* pl, int pa)
        : x(px), y(py), label(pl), app(pa), deleted(false) {}
};

static DesktopIcon s_icons[] = {
    DesktopIcon(14, 14, "文件管理器", APP_FILEMGR),
    DesktopIcon(14 + ICON_W + 8, 14, "终端", APP_TERMINAL),
    DesktopIcon(14 + 2 * (ICON_W + 8), 14, "计算器", APP_CALC),
    DesktopIcon(14, 14 + ICON_H + 10, "文本查看器", APP_TEXTVIEW),
    DesktopIcon(14 + ICON_W + 8, 14 + ICON_H + 10, "系统信息", APP_SYSINFO),
    DesktopIcon(14, 14 + 2 * (ICON_H + 10), "设置", APP_SETTINGS),
    DesktopIcon(14 + ICON_W + 8, 14 + 2 * (ICON_H + 10), "软件商店", APP_STORE),
    DesktopIcon(14 + 2 * (ICON_W + 8), 14 + 2 * (ICON_H + 10), "图片查看器", APP_IMAGEVIEWER),
    DesktopIcon(14, 14 + 3 * (ICON_H + 10), "音乐播放器", APP_MUSIC),
    DesktopIcon(14 + ICON_W + 8, 14 + 3 * (ICON_H + 10), "系统监视器", APP_MONITOR),
    DesktopIcon(14 + 2 * (ICON_W + 8), 14 + 3 * (ICON_H + 10), "浏览器", APP_BROWSER),
    DesktopIcon(14, 14 + 4 * (ICON_H + 10), "网络", APP_NETCFG),
    DesktopIcon(14 + ICON_W + 8, 14 + 4 * (ICON_H + 10), "应用启动器", APP_NEFUD),
};
static const int s_icon_count = (int)(sizeof(s_icons) / sizeof(s_icons[0]));

// desktop icon labels follow the system UI language
static const char* icon_label(int app) {
    switch (app) {
    case APP_FILEMGR:    return T("文件管理器", "Files");
    case APP_TERMINAL:   return T("终端", "Terminal");
    case APP_CALC:       return T("计算器", "Calculator");
    case APP_TEXTVIEW:   return T("文本查看器", "Text View");
    case APP_SYSINFO:    return T("系统信息", "System Info");
    case APP_SETTINGS:   return T("设置", "Settings");
    case APP_STORE:      return T("软件商店", "Store");
    case APP_IMAGEVIEWER:return T("图片查看器", "Images");
    case APP_MUSIC:      return T("音乐播放器", "Music");
    case APP_MONITOR:    return T("系统监视器", "Monitor");
    case APP_BROWSER:    return T("浏览器", "Browser");
    case APP_NETCFG:     return T("网络", "Network");
    case APP_NEFUD:      return T("应用启动器", "Launcher");
    default: return "?";
    }
}

static bool s_start_open = false;
static int s_sel_icon = -1;
static int s_last_click_icon = -1;
static uint32_t s_last_click_time = 0;
static int s_start_hover = -1;
static int s_start_btn_hover = 0;
static uint8_t s_last_buttons = 0;
// desktop icon context menu
static bool s_rmenu_open = false;
static int s_rmenu_x = 0, s_rmenu_y = 0;
static int s_rmenu_icon = -1;
static int s_rmenu_hover = -1;
// icon drag
static int s_drag_icon = -1;

static uint32_t lerp_color(uint32_t a, uint32_t b, int t, int max) {
    if (max <= 0) return b;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ar + ((br - ar) * t) / max;
    int g = ag + ((bg - ag) * t) / max;
    int bl = ab + ((bb - ab) * t) / max;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)bl;
}

static void paint_wallpaper(Surface& fb) {
    int W = fb.width, H = fb.height;
    uint32_t top = 0x00345C86, bottom = 0x0088B7D8, base = color::CREAM;
    wallpaper_colors(g_settings.wallpaper, &top, &bottom, &base);
    for (int y = 0; y < H; y++) {
        uint32_t c;
        if (y < 180) c = lerp_color(top, bottom, y, 180);
        else if (y < 220) c = lerp_color(bottom, base, y - 180, 40);
        else c = base;
        uint32_t* row = (uint32_t*)(fb.addr + (size_t)y * (size_t)fb.pitch);
        for (int x = 0; x < W; x++) row[x] = c;
    }
    // 右下角水印
    gfx::text(fb, W - 132, H - 52, "nefuOS 0.1", 0x00C9C8C2, 0x00000000);
}

static void draw_icon_tile(Surface& fb, int tx, int ty, int app) {
    gfx::fillrect(fb, tx, ty, TILE, TILE, color::WHITE);
    gfx::rect(fb, tx, ty, TILE, TILE, 0x00B0AFA8);
    switch (app) {
    case APP_FILEMGR: {
        gfx::fillrect(fb, tx + 8, ty + 18, 36, 24, color::YELLOW);
        gfx::fillrect(fb, tx + 8, ty + 12, 18, 8, color::YELLOW);
        gfx::rect(fb, tx + 8, ty + 18, 36, 24, 0x008C7A20);
        gfx::rect(fb, tx + 8, ty + 12, 18, 8, 0x008C7A20);
        break;
    }
    case APP_TERMINAL: {
        gfx::fillrect(fb, tx + 8, ty + 10, 36, 32, 0x00101418);
        gfx::rect(fb, tx + 8, ty + 10, 36, 32, 0x00505A66);
        gfx::text(fb, tx + 12, ty + 18, ">_", color::GREEN, 0x00101418);
        break;
    }
    case APP_CALC: {
        gfx::fillrect(fb, tx + 10, ty + 8, 32, 36, color::BLUE);
        gfx::fillrect(fb, tx + 14, ty + 12, 24, 6, color::WHITE);
        gfx::hline(fb, tx + 12, tx + 39, ty + 26, color::WHITE);
        gfx::hline(fb, tx + 12, tx + 39, ty + 32, color::WHITE);
        gfx::vline(fb, tx + 25, ty + 22, ty + 38, color::WHITE);
        break;
    }
    case APP_TEXTVIEW: {
        gfx::fillrect(fb, tx + 14, ty + 8, 26, 36, color::WHITE);
        gfx::rect(fb, tx + 14, ty + 8, 26, 36, color::BORDER);
        gfx::hline(fb, tx + 16, tx + 38, ty + 18, 0x00C9C8C2);
        gfx::hline(fb, tx + 16, tx + 38, ty + 24, 0x00C9C8C2);
        gfx::hline(fb, tx + 16, tx + 38, ty + 30, 0x00C9C8C2);
        break;
    }
    case APP_SYSINFO: {
        gfx::fillrect(fb, tx + 12, ty + 10, 28, 32, color::TEAL);
        gfx::rect(fb, tx + 12, ty + 10, 28, 32, 0x00207A6C);
        gfx::text(fb, tx + 24, ty + 16, "i", color::WHITE, color::TEAL);
        break;
    }
    case APP_SETTINGS: {
        // 齿轮
        gfx::fillcircle(fb, tx + 26, ty + 26, 10, 0x00666C74);
        gfx::fillcircle(fb, tx + 26, ty + 26, 6, color::WHITE);
        gfx::fillrect(fb, tx + 23, ty + 13, 6, 8, 0x00666C74);
        gfx::fillrect(fb, tx + 23, ty + 31, 6, 8, 0x00666C74);
        gfx::fillrect(fb, tx + 13, ty + 23, 8, 6, 0x00666C74);
        gfx::fillrect(fb, tx + 31, ty + 23, 8, 6, 0x00666C74);
        break;
    }
    case APP_STORE: {
        // 购物袋
        gfx::fillrect(fb, tx + 10, ty + 16, 32, 24, 0x00E67E22);
        gfx::fillrect(fb, tx + 17, ty + 12, 18, 8, 0x00E67E22);
        gfx::rect(fb, tx + 10, ty + 16, 32, 24, 0x00A85E15);
        gfx::rect(fb, tx + 17, ty + 12, 18, 8, 0x00A85E15);
        gfx::text(fb, tx + 20, ty + 24, "S", color::WHITE, 0x00E67E22);
        break;
    }
    case APP_IMAGEVIEWER: {
        // 相框 + 山 + 太阳
        gfx::fillrect(fb, tx + 8, ty + 8, 36, 36, 0x00F5F1E8);
        gfx::rect(fb, tx + 8, ty + 8, 36, 36, 0x008C7A20);
        gfx::fillcircle(fb, tx + 15, ty + 15, 4, color::ORANGE);
        gfx::fillrect(fb, tx + 10, ty + 34, 32, 8, 0x0030A14A);
        gfx::line(fb, tx + 10, ty + 34, tx + 22, ty + 22, 0x006B7280);
        gfx::line(fb, tx + 22, ty + 22, tx + 32, ty + 32, 0x006B7280);
        break;
    }
    case APP_MUSIC: {
        // 音符
        gfx::fillrect(fb, tx + 14, ty + 10, 22, 26, 0x0014181E);
        gfx::rect(fb, tx + 14, ty + 10, 22, 26, 0x00505A66);
        gfx::fillcircle(fb, tx + 19, ty + 34, 4, color::WHITE);
        gfx::fillcircle(fb, tx + 30, ty + 34, 4, color::WHITE);
        gfx::vline(fb, tx + 19, ty + 14, ty + 34, color::WHITE);
        gfx::vline(fb, tx + 30, ty + 14, ty + 34, color::WHITE);
        gfx::hline(fb, tx + 19, tx + 30, ty + 14, color::WHITE);
        break;
    }
    case APP_MONITOR: {
        // 仪表 + 曲线
        gfx::fillrect(fb, tx + 8, ty + 10, 36, 32, 0x0014181E);
        gfx::rect(fb, tx + 8, ty + 10, 36, 32, 0x00505A66);
        gfx::line(fb, tx + 10, ty + 36, tx + 18, ty + 28, color::GREEN);
        gfx::line(fb, tx + 18, ty + 28, tx + 26, ty + 32, color::GREEN);
        gfx::line(fb, tx + 26, ty + 32, tx + 42, ty + 16, color::GREEN);
        gfx::hline(fb, tx + 10, tx + 42, ty + 38, 0x00505A66);
        break;
    }
    case APP_BROWSER: {
        // globe
        gfx::circle(fb, tx + 26, ty + 26, 15, color::BLUE_LT);
        gfx::line(fb, tx + 11, ty + 26, tx + 41, ty + 26, color::BLUE_LT);
        gfx::line(fb, tx + 15, ty + 15, tx + 37, ty + 37, color::BLUE_LT);
        gfx::line(fb, tx + 37, ty + 15, tx + 15, ty + 37, color::BLUE_LT);
        break;
    }
    case APP_NETCFG: {
        // signal bars
        for (int i = 0; i < 4; i++) {
            int bh = 6 + i * 6;
            gfx::fillrect(fb, tx + 8 + i * 10, ty + 40 - bh, 7, bh, i < 3 ? color::GREEN : color::BLUE_LT);
        }
        break;
    }
    case APP_NEFUD: {
        // package box
        gfx::fillrect(fb, tx + 8, ty + 8, 36, 34, color::ORANGE);
        gfx::fillrect(fb, tx + 8, ty + 8, 36, 8, color::YELLOW);
        gfx::line(fb, tx + 8, ty + 20, tx + 44, ty + 20, color::WHITE);
        gfx::line(fb, tx + 26, ty + 8, tx + 26, ty + 42, color::WHITE);
        break;
    }
    default: break;
    }
}

static void paint_icons(Surface& fb) {
    for (int i = 0; i < s_icon_count; i++) {
        DesktopIcon& ic = s_icons[i];
        if (ic.deleted) continue;
        if (s_sel_icon == i) {
            gfx::fillrect(fb, ic.x, ic.y, ICON_W, ICON_H, 0x20FFFFFF);
            gfx::rect(fb, ic.x, ic.y, ICON_W, ICON_H, 0x50FFFFFF);
        }
        int tx = ic.x + (ICON_W - TILE) / 2;
        int ty = ic.y + 2;
        draw_icon_tile(fb, tx, ty, ic.app);
        const char* lbl = icon_label(ic.app);
        int tw = gfx::text_width(lbl);
        int lx = ic.x + (ICON_W - tw) / 2;
        gfx::fillrect(fb, lx - 2, ty + TILE + 6, tw + 4, 17, 0x50000000);
        gfx::text(fb, lx, ty + TILE + 8, lbl, color::WHITE, 0x50000000);
    }
    // desktop context menu
    if (s_rmenu_open) {
        const char* items[2] = { "Open", "Delete Shortcut" };
        int mw = 150, mh = 2 * 22 + 6;
        int mx = s_rmenu_x, my = s_rmenu_y;
        if (mx + mw > fb.width) mx = fb.width - mw - 4;
        if (my + mh > fb.height - TASKBAR_H) my = fb.height - TASKBAR_H - mh - 4;
        gfx::fillrect(fb, mx, my, mw, mh, color::WHITE);
        gfx::rect(fb, mx, my, mw, mh, color::BORDER);
        for (int i = 0; i < 2; i++) {
            int iy = my + 3 + i * 22;
            uint32_t bg = (s_rmenu_hover == i) ? 0x00D8E6F5 : color::WHITE;
            gfx::fillrect(fb, mx + 1, iy, mw - 2, 22, bg);
            gfx::text(fb, mx + 10, iy + 3, items[i], color::TEXT, bg);
        }
    }
}

static void paint_taskbar(Surface& fb) {
    if (!g_settings.show_taskbar) return;
    int W = fb.width, H = fb.height;
    int y0 = H - TASKBAR_H;
    gfx::fillrect(fb, 0, y0, W, TASKBAR_H, 0x00262A31);
    gfx::hline(fb, 0, W - 1, y0, 0x004F5A66);
    // 开始按钮
    int sx = 4, sy = y0 + 3, sw = 64, sh = 24;
    uint32_t accent = accent_color(g_settings.accent);
    uint32_t sbc = s_start_btn_hover ? 0x004A90C2 : accent;
    gfx::fillrect(fb, sx, sy, sw, sh, sbc);
    gfx::rect(fb, sx, sy, sw, sh, 0x002F6FB6);
    gfx::text(fb, sx + 8, sy + 4, "Start", color::WHITE, sbc);
    if (s_start_open) gfx::fillrect(fb, sx, sy, sw, 2, color::WHITE);
    // 任务按钮
    int bx = sx + sw + 8;
    for (int i = 0; i < g_wm->windows().size(); i++) {
        Window* w = g_wm->windows()[i];
        if (!w->visible || w->closed) continue;
        int tl = w->title.len();
        if (tl > 12) tl = 12;
        int wd = tl * 8 + 16;
        bool active = (g_wm->focus() == w);
        uint32_t bg = active ? accent : 0x003B4654;
        gfx::fillrect(fb, bx, sy, wd, sh, bg);
        gfx::rect(fb, bx, sy, wd, sh, 0x00262A31);
        gfx::text(fb, bx + 8, sy + 4, w->title.c_str(), color::WHITE, bg);
        if (w->minimized) {
            gfx::hline(fb, bx + 4, bx + wd - 5, sy + sh - 4, color::WHITE);
        }
        bx += wd + 4;
    }
    // 时钟
    if (g_settings.show_clock) {
        uint32_t sec = platform_seconds_of_day();
        char buf[16];
        ksprintf(buf, sizeof(buf), "%02u:%02u", (sec / 3600) % 24, (sec / 60) % 60);
        int tw = gfx::text_width(buf);
        gfx::text(fb, W - tw - 10, y0 + 7, buf, color::WHITE, 0x00262A31);
    }
}

// 开始菜单条目：内置应用 + 设置 + 商城 + 已安装商店应用
static int build_menu(int* list) {
    int n = 0;
    for (int i = 0; i < APP_BUILTIN_COUNT; i++) list[n++] = i;
    list[n++] = APP_SETTINGS;
    list[n++] = APP_STORE;
    int ids[8];
    int cnt = app_installed_list(ids, 8);
    for (int i = 0; i < cnt; i++) list[n++] = ids[i];
    return n;
}

static void paint_start_menu(Surface& fb) {
    if (!s_start_open) return;
    int menu_apps[16];
    int rows = build_menu(menu_apps);
    int H = fb.height;
    int mw = 196;
    int mh = rows * 26 + 10 + 32; // 应用行 + 分隔 + Power Off
    int mx = 4, my = H - TASKBAR_H - mh;
    if (my < 0) my = 0;
    gfx::fillrect(fb, mx, my, mw, mh, color::WHITE);
    gfx::rect(fb, mx, my, mw, mh, color::BORDER);
    int y = my + 5;
    for (int i = 0; i < rows; i++) {
        bool hover = (s_start_hover == i);
        uint32_t bg = hover ? 0x00D8E6F5 : color::WHITE;
        gfx::fillrect(fb, mx + 3, y, mw - 6, 24, bg);
        gfx::text(fb, mx + 12, y + 4, app_name(menu_apps[i]), hover ? color::BLUE : color::TEXT, bg);
        y += 26;
    }
    gfx::hline(fb, mx + 8, mx + mw - 8, y + 2, color::BORDER);
    y += 8;
    bool hover = (s_start_hover == rows);
    uint32_t bg = hover ? 0x00F5DEDC : color::WHITE;
    gfx::fillrect(fb, mx + 3, y, mw - 6, 24, bg);
    gfx::text(fb, mx + 12, y + 4, "Power Off", color::RED, bg);
}

void desktop_init() {
    s_start_open = false;
    s_sel_icon = -1;
}

void desktop_paint(Surface& fb) {
    paint_wallpaper(fb);
    paint_icons(fb);
    paint_taskbar(fb);
    paint_start_menu(fb);
}

void desktop_paint_boot(Surface& fb) {
    fb.fill(0x0012141A);
    int W = fb.width, H = fb.height;
    // 大标题
    gfx::text_scale(fb, W / 2 - 4 * 8 * 4, H / 2 - 90, "nefuOS", color::WHITE, 0x0012141A, 4);
    gfx::text(fb, W / 2 - 52, H / 2 - 18, "v0.1.0  C++/C  tiny OS", color::BLUE_LT, 0x0012141A);
    // 进度条
    int bw = 300, bx = W / 2 - bw / 2, by = H / 2 + 16;
    gfx::rect(fb, bx, by, bw, 12, 0x00404A58);
    int p = (platform_tick_ms() / 40) % (bw - 8);
    gfx::fillrect(fb, bx + 3, by + 3, p, 6, color::BLUE_LT);
    gfx::text(fb, W / 2 - 30, by + 18, "Loading...", color::TEXT2, 0x0012141A);
}

bool desktop_handle_mouse(int x, int y, uint8_t buttons) {
    int H = platform_screen()->height;
    bool pressed = (buttons & 1) != 0;
    bool released = (buttons == 0) && (s_last_buttons != 0);
    bool r_pressed = (buttons & 2) != 0 && (s_last_buttons & 2) == 0;
    s_last_buttons = buttons;
    uint32_t now = platform_tick_ms();
    int menu_apps[16];

    // desktop icon context menu
    if (s_rmenu_open) {
        int mw = 150, mh = 2 * 22 + 6;
        int mx = s_rmenu_x, my = s_rmenu_y;
        if (mx + mw > platform_screen()->width) mx = platform_screen()->width - mw - 4;
        if (my + mh > H - TASKBAR_H) my = H - TASKBAR_H - mh - 4;
        if (x >= mx && x < mx + mw && y >= my && y < my + mh) {
            int idx = (y - my - 3) / 22;
            s_rmenu_hover = (idx >= 0 && idx < 2) ? idx : -1;
            if (pressed) {
                if (idx == 0 && s_rmenu_icon >= 0) {
                    DesktopIcon& ic = s_icons[s_rmenu_icon];
                    if (!ic.deleted) app_launch(ic.app);
                } else if (idx == 1 && s_rmenu_icon >= 0) {
                    s_icons[s_rmenu_icon].deleted = true;   // shortcut only
                    if (s_sel_icon == s_rmenu_icon) s_sel_icon = -1;
                }
                s_rmenu_open = false;
                s_rmenu_icon = -1;
                s_rmenu_hover = -1;
            }
            return true;
        }
        if (pressed) { s_rmenu_open = false; s_rmenu_icon = -1; }
    }

    // 开始菜单打开时优先
    if (s_start_open) {
        int rows = build_menu(menu_apps);
        int mw = 196, mh = rows * 26 + 10 + 32;
        int mx = 4, my = H - TASKBAR_H - mh;
        if (my < 0) my = 0;
        if (x >= mx && x < mx + mw && y >= my && y < my + mh) {
            int row = (y - my - 5) / 26;
            if (row < 0 || row >= rows) row = -1;
            s_start_hover = row;
            if (pressed && row >= 0) {
                s_start_open = false;
                s_start_hover = -1;
                if (row < rows) app_launch(menu_apps[row]);
                else if (row == rows) {
                    nefuos_shutdown();
                    platform_poweroff();
                }
                return true;
            }
            return true;
        }
        if (pressed) { s_start_open = false; s_start_hover = -1; }
    }

    // 任务栏（隐藏时跳过）
    if (g_settings.show_taskbar && y >= H - TASKBAR_H) {
        if (pressed) s_start_open = false;
        s_start_btn_hover = 0;
        // 开始按钮
        if (x >= 4 && x < 68 && y >= H - 27 && y < H - 3) {
            s_start_btn_hover = 1;
            if (pressed) s_start_open = !s_start_open;
            return true;
        }
        // 任务按钮
        int bx = 76;
        for (int i = 0; i < g_wm->windows().size(); i++) {
            Window* w = g_wm->windows()[i];
            if (!w->visible || w->closed) continue;
            int tl = w->title.len();
            if (tl > 12) tl = 12;
            int wd = tl * 8 + 16;
            if (x >= bx && x < bx + wd && y >= H - 28 && y < H - 2) {
                if (pressed) {
                    if (w->minimized) { w->minimized = false; g_wm->raise(w); }
                    else if (g_wm->focus() == w) w->minimized = true;
                    else g_wm->raise(w);
                }
                return true;
            }
            bx += wd + 4;
        }
        return true; // 任务栏空白处也消费
    }

    // 任何窗口覆盖处优先交给窗口管理器（否则桌面图标会吞掉窗口上的点击）
    if (g_wm->hit(x, y)) return false;

    // desktop icons: click / right-click / drag
    if (r_pressed) {
        for (int i = 0; i < s_icon_count; i++) {
            DesktopIcon& ic = s_icons[i];
            if (ic.deleted) continue;
            if (x >= ic.x && x < ic.x + ICON_W && y >= ic.y && y < ic.y + ICON_H) {
                s_sel_icon = i;
                s_rmenu_open = true;
                s_rmenu_x = x;
                s_rmenu_y = y;
                s_rmenu_icon = i;
                s_rmenu_hover = -1;
                return true;
            }
        }
        s_rmenu_open = false;
        return true;
    }

    if (s_drag_icon >= 0) {
        DesktopIcon& ic = s_icons[s_drag_icon];
        if (released) {
            // round to the nearest grid cell (handles negative deltas correctly)
            int col = (ic.x - 14 + (ICON_W + 8) / 2) / (ICON_W + 8);
            int row = (ic.y - 14 + (ICON_H + 10) / 2) / (ICON_H + 10);
            if (col < 0) col = 0;
            if (row < 0) row = 0;
            // find the nearest free cell so icons never overlap
            int best_c = col, best_r = row, best_d2 = 0x7FFFFFFF;
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    bool taken = false;
                    for (int k = 0; k < s_icon_count; k++) {
                        if (k == s_drag_icon || s_icons[k].deleted) continue;
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
            if (ic.y + ICON_H > H - TASKBAR_H) ic.y = H - TASKBAR_H - ICON_H;
            s_drag_icon = -1;
            return true;
        }
        ic.x = x - ICON_W / 2;
        ic.y = y - ICON_H / 2;
        if (ic.x < 4) ic.x = 4;
        if (ic.y < 4) ic.y = 4;
        return true;
    }

    for (int i = 0; i < s_icon_count; i++) {
        DesktopIcon& ic = s_icons[i];
        if (ic.deleted) continue;
        if (x >= ic.x && x < ic.x + ICON_W && y >= ic.y && y < ic.y + ICON_H) {
            if (pressed) {
                bool dbl = (s_last_click_icon == i && now - s_last_click_time < 400);
                s_last_click_icon = i;
                s_last_click_time = now;
                s_sel_icon = i;
                if (dbl) app_launch(ic.app);
                else s_drag_icon = i;
            }
            return true;
        }
    }

    if (pressed) { s_sel_icon = -1; s_start_open = false; }
    return false;
}

bool desktop_handle_key(int keycode, char ascii) {
    (void)ascii;
    if (s_start_open && keycode == KEY_ESC) {
        s_start_open = false;
        return true;
    }
    return false;
}

} // namespace nefu
