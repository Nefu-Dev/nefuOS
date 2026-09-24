// ============================================================
// nefuOS BIOS Setup Utility
// Classic blue style - keyboard-first with mouse assist - multi-page configuration
// ------------------------------------------------------------
// Structure overview (read this first, then the code below):
//   1) State model  BiosState - tracks the current page, the selected item of each page, the edit state,
//      the confirm dialog, the toast message, and a "working copy" config s_work.
//      Working-copy rationale: all changes land on s_work first; only selecting
//      "Save & Exit" really writes to the system (g_uefi / g_settings).
//   2) Rendering  one paint function per page, all drawn on the window back
//      buffer; the window manager repaints every frame, so after changing state you just
//      repaint. The entry is bios_paint(), which dispatches by the current page and
//      finally draws the confirm dialog on top.
//   3) Input  bios_on_key receives all keyboard input: first the confirm dialog, then
//      inline editing, and finally per-page navigation / adjustment dispatch.
//      bios_on_mouse supports picking menu items with the mouse (optional convenience).
//   4) Persistence  bios_apply() writes the working copy into g_uefi (UEFI config)
//      and g_settings (desktop settings), then settings_save() persists them;
//      bios_discard() drops the working copy and reverts to the current system config.
//   5) Page details:
//        MAIN     main menu (6 items: enter sub-pages / save & exit / exit without saving)
//        SYSTEM   system overview: CPU/memory/firmware/time/disk (read-only)
//        BOOT     boot settings: timeout, splash, boot/lock wallpapers, mode
//        SECURITY security: username, set password (double-confirm), clear password
//        POWER    power management: lock on suspend, idle auto-lock (really persisted)
//        EXIT     exit menu: save & exit / discard & exit / restore defaults
// ------------------------------------------------------------
// Learning note: this file is a "header-only implementation" - all functions are static and live
// in the header, one copy per .cpp translation unit that includes it. It depends on no
// global singleton; the only external state is the system-provided g_uefi / g_settings.
// ============================================================
#pragma once
#include "../gui/gfx.h"
#include "../gui/wm.h"
#include "../platform.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../sys/sha256.h"
#include "../klib/klib.h"

namespace nefu { namespace apps {

// ==================== Classic BIOS palette ====================
static const uint32_t BIOS_BG       = 0x000000AA;  // classic blue background
static const uint32_t BIOS_TITLE_BG  = 0x00000080; // dark blue title bar
static const uint32_t BIOS_TXT       = 0x00FFFFFF; // white body text
static const uint32_t BIOS_TXT_DIM   = 0x0099CCFF; // light blue explanatory text
static const uint32_t BIOS_TXT_BLUE  = 0x000000AA; // blue text when highlighted (inverse)
static const uint32_t BIOS_SEL_BG    = 0x00FFFFFF; // white background for the selected item
static const uint32_t BIOS_AMBER     = 0x00FFCC33; // amber accent (value area)
static const uint32_t BIOS_OK        = 0x0033FF66; // green (normal / saved)
static const uint32_t BIOS_WARN      = 0x00FF9933; // orange (unsaved-changes hint)
static const uint32_t BIOS_ERR       = 0x00FF5555; // red (error hint)
static const uint32_t BIOS_BORDER    = 0x008888FF; // panel border

// ==================== Page IDs ====================
enum BiosPage {
    PAGE_MAIN = 0,     // main menu
    PAGE_SYSTEM,       // system overview (read-only info)
    PAGE_BOOT,         // boot settings
    PAGE_BOOT_ADD,     // add boot entry (sub-page, multi-boot)
    PAGE_BOOT_DEL,     // remove boot entry (sub-page)
    PAGE_SECURITY,     // security settings
    PAGE_POWER,        // power management
    PAGE_EXIT,         // exit menu
    PAGE_COUNT
};

// ==================== Inline edit modes ====================
enum BiosEdit {
    EDIT_NONE = 0,        // not editing
    EDIT_USERNAME,        // editing username
    EDIT_PWD_NEW,         // entering new password (masked)
    EDIT_PWD_CONFIRM,     // re-entering confirm password
    EDIT_BOOT_NAME        // editing boot entry name
};

// ==================== Confirm dialog types ====================
enum BiosConfirm {
    CF_NONE = 0,
    CF_EXIT_SAVE,         // save and exit?
    CF_EXIT_DISCARD,      // exit without saving?
    CF_CLEAR_PWD,         // clear password?
    CF_LOAD_DEFAULTS      // restore factory defaults?
};

// ==================== BIOS runtime state ====================
struct BiosState {
    BiosPage   page;                  // current page
    int        sel[PAGE_COUNT];       // selected item for each page
    bool       running;               // whether BIOS is still running
    bool       dirty;                 // whether there are unsaved changes
    // ---- inline editing ----
    BiosEdit   edit;                  // edit mode
    char       edit_buf[40];          // edit buffer (username / password)
    int        edit_len;              // buffer length
    // ---- confirm dialog ----
    BiosConfirm confirm;              // current confirm type
    // ---- toast message (auto-clears after 3 s) ----
    char       msg[64];
    uint32_t   msg_tick;              // system tick when the message was set
    // ---- working copy for the power page (dropped if not saved on exit) ----
    bool       lock_on_suspend;       // lock screen on suspend
    int        idle_lock_sec;         // idle auto-lock (seconds, 0 = off)
    // ---- boot page ----
    int        boot_first;            // index of the first boot device in the disk list
    // ---- add-boot-entry sub-page ----
    int        boot_add_dev;          // target device index (disk list + final "nefuOS" entry)
    int        boot_add_kind;         // kind: 0=OS 1=Disk 2=ISO
    char       boot_add_name[32];     // boot entry name (editable)
};

// Global BIOS state (file-local static: one copy per translation unit, isolated from each other)
static BiosState s_bios;
static UefiConfig s_work;             // UEFI config working copy
static char      s_new_pwd[40];       // staging for the first password entry (for double-confirm)
static bool      s_bios_open = false; // whether the window is open (prevents duplicate creation)
// (window pointer no longer kept: s_bios_open prevents duplicates)

// ==================== Constants and helpers ====================
static const int BIOS_MENU_TOP = 72;   // menu area start Y (leaves room for the top tab strip)
static const int BIOS_ROW_H    = 26;   // menu row height
static const int BIOS_EDIT_MAX = 31;   // max username length

// top tab strip layout (mimics real-BIOS page tabs)
static const int BIOS_TAB_Y    = 40;   // tab strip start Y
static const int BIOS_TAB_H    = 22;   // tab strip height
static const int BIOS_TAB_W    = 108;  // width of each tab
static const int BIOS_TAB_N    = 5;    // number of tabs

// boot wallpaper candidates (matching g_settings indices 0..3)
static const char* k_wallpapers[] = { "blue", "sunset", "dark", "night" };
static const int k_wallpaper_count = 4;

// map a wallpaper name to a settings index (-1 if not found)
static int bios_wallpaper_index(const char* name) {
    for (int i = 0; i < k_wallpaper_count; i++)
        if (strcmp(name, k_wallpapers[i]) == 0) return i;
    return -1;
}

// mark that there are unsaved changes
static void bios_mark_dirty() {
    s_bios.dirty = true;
}

// set a bottom toast message (tick-based, auto-clears after the timeout)
static void bios_set_msg(const char* msg) {
    strncpy(s_bios.msg, msg, sizeof(s_bios.msg) - 1);
    s_bios.msg[sizeof(s_bios.msg) - 1] = 0;
    s_bios.msg_tick = platform_tick_ms();
}

// draw the title bar: dark blue background + left title + right version
static void bios_title_bar(Surface& s, const char* title) {
    gfx::fillrect(s, 0, 0, s.width, 36, BIOS_TITLE_BG);
    gfx::fillrect(s, 0, 36, s.width, 2, 0x00000066);
    gfx::text(s, 18, 10, title, BIOS_TXT, BIOS_TITLE_BG);
    char ver[48];
    ksprintf(ver, sizeof(ver), "nefuOS BIOS v2.0.0");
    int vw = gfx::text_width(ver);
    gfx::text(s, s.width - vw - 18, 10, ver, BIOS_TXT_DIM, BIOS_TITLE_BG);
}

// draw the bottom help bar: dark blue background + two key-hint lines + status
static void bios_help_bar(Surface& s, const char* line1, const char* line2) {
    int H = s.height;
    gfx::fillrect(s, 0, H - 64, s.width, 64, BIOS_TITLE_BG);
    gfx::fillrect(s, 0, H - 66, s.width, 2, 0x00000066);
    if (line1) gfx::text(s, 18, H - 58, line1, BIOS_TXT, BIOS_TITLE_BG);
    if (line2) gfx::text(s, 18, H - 38, line2, BIOS_TXT_DIM, BIOS_TITLE_BG);
    // right side: unsaved-changes marker
    if (s_bios.dirty) {
        const char* d = "* Unsaved changes";
        int dw = gfx::text_width(d);
        gfx::text(s, s.width - dw - 18, H - 58, d, BIOS_WARN, BIOS_TITLE_BG);
    }
    // right side, second line: toast message (if any)
    if (s_bios.msg[0]) {
        uint32_t now = platform_tick_ms();
        if (s_bios.msg_tick && now - s_bios.msg_tick < 3000) {
            int mw = gfx::text_width(s_bios.msg);
            gfx::text(s, s.width - mw - 18, H - 38, s_bios.msg, BIOS_OK, BIOS_TITLE_BG);
        } else {
            s_bios.msg[0] = 0;
        }
    }
}

// draw a panel: border + inner background + panel title
static void bios_panel(Surface& s, int x, int y, int w, int h, const char* title) {
    gfx::rect(s, x, y, w, h, BIOS_BORDER);
    gfx::fillrect(s, x + 1, y + 1, w - 2, h - 2, BIOS_BG);
    gfx::text(s, x + 10, y + 8, title, BIOS_TXT_DIM, BIOS_BG);
    gfx::hline(s, x + 6, x + w - 6, y + 26, BIOS_BORDER);
}

// draw the top tab strip: active page on white, the rest on grey-blue; clickable to jump pages
static void bios_tab_strip(Surface& s) {
    const char* tabs[BIOS_TAB_N] = { "Main", "Boot", "Security", "Power", "Exit" };
    const BiosPage pages[BIOS_TAB_N] = { PAGE_MAIN, PAGE_BOOT, PAGE_SECURITY, PAGE_POWER, PAGE_EXIT };
    int total = BIOS_TAB_W * BIOS_TAB_N;
    int x0 = (s.width - total) / 2;
    for (int i = 0; i < BIOS_TAB_N; i++) {
        int tx = x0 + i * BIOS_TAB_W;
        bool active = (s_bios.page == pages[i]);
        gfx::fillrect(s, tx, BIOS_TAB_Y, BIOS_TAB_W, BIOS_TAB_H,
                      active ? BIOS_TXT : 0x00005588);
        gfx::rect(s, tx, BIOS_TAB_Y, BIOS_TAB_W, BIOS_TAB_H, BIOS_BORDER);
        uint32_t fg = active ? BIOS_TXT_BLUE : BIOS_TXT_DIM;
        uint32_t bg = active ? BIOS_TXT : 0x00005588;
        int tw = gfx::text_width(tabs[i]);
        gfx::text(s, tx + (BIOS_TAB_W - tw) / 2, BIOS_TAB_Y + 4, tabs[i], fg, bg);
    }
}

// draw one menu / settings row: selectable, editable, with a value and a key hint
// label is left-aligned; value is right-aligned at value_x
static void bios_draw_item(Surface& s, int x, int y, int value_x,
                           const char* label, const char* value,
                           bool selected, bool editing, const char* hint) {
    int w = s.width;
    int row_h = BIOS_ROW_H;
    // background: white when selected, otherwise transparent (keeping the blue background)
    if (selected) gfx::fillrect(s, x - 8, y, w - x - 20, row_h, BIOS_SEL_BG);
    // highlight bar: 3px amber indicator on the left (classic BIOS style)
    gfx::fillrect(s, x - 14, y, 3, row_h, selected ? BIOS_AMBER : BIOS_BG);
    // label
    uint32_t fg = selected ? BIOS_TXT_BLUE : BIOS_TXT;
    uint32_t bg = selected ? BIOS_SEL_BG : BIOS_BG;
    gfx::text(s, x, y + 5, label, fg, bg);
    // value (right-aligned); shows mask/buffer while editing
    if (value && value[0]) {
        char valbuf[48];
        if (editing) {
            // while editing: mask passwords, show the buffer + cursor for usernames
            bool masked = (s_bios.edit == EDIT_PWD_NEW || s_bios.edit == EDIT_PWD_CONFIRM);
            int n = 0;
            for (int i = 0; i < s_bios.edit_len && n < (int)sizeof(valbuf) - 2; i++)
                valbuf[n++] = masked ? '*' : s_bios.edit_buf[i];
            valbuf[n] = 0;
            int vw = gfx::text_width(valbuf);
            // cursor: a blinking block on the right (blink driven by tick)
            uint32_t now = platform_tick_ms();
            if ((now / 500) % 2 == 0) {
                gfx::fillrect(s, value_x + vw, y + 4, 8, 16, selected ? BIOS_TXT_BLUE : BIOS_TXT);
            }
            gfx::text(s, value_x, y + 5, valbuf, fg, bg);
        } else {
            // normal display: truncate overlong text
            strncpy(valbuf, value, sizeof(valbuf) - 1);
            valbuf[sizeof(valbuf) - 1] = 0;
            int vw = gfx::text_width(valbuf);
            if (value_x + vw > w - 30) {
                int keep = (w - 30 - value_x) / 8;
                if (keep < 0) keep = 0;
                valbuf[keep] = 0;
            }
            gfx::text(s, value_x, y + 5, valbuf, selected ? BIOS_TXT_BLUE : BIOS_AMBER, bg);
        }
    }
    // key hint (e.g. [+/-] or [Enter]), placed right of the value
    if (hint && hint[0]) {
        int hw = gfx::text_width(hint);
        uint32_t hc = selected ? 0x00333399 : BIOS_TXT_DIM;
        gfx::text(s, w - hw - 24, y + 5, hint, hc, bg);
    }
}

// draw a read-only info row (left label + right value), for the system overview
static void bios_info_row(Surface& s, int x, int y, const char* label, const char* value, uint32_t color) {
    gfx::text(s, x, y, label, BIOS_TXT_DIM, BIOS_BG);
    int lw = gfx::text_width(label);
    gfx::text(s, x + lw + 12, y, value, color ? color : BIOS_TXT, BIOS_BG);
}

// ==================== Per-page rendering ====================

// ---- main menu ----
static void bios_paint_main(Surface& s) {
    bios_title_bar(s, "nefuOS BIOS Setup Utility");
    int x = 30;
    int y = BIOS_MENU_TOP;
    int sel = s_bios.sel[PAGE_MAIN];

    // left: main menu items (the tab strip already labels each section; no repeated page titles here)
    const char* items[] = {
        "System Overview",
        "Boot Settings",
        "Security Settings",
        "Power Management",
        "Save Changes and Exit",
        "Exit Without Saving"
    };
    for (int i = 0; i < 6; i++) {
        bios_draw_item(s, x, y + i * BIOS_ROW_H, 260, items[i], 0, i == sel, false, 0);
    }

    // right: system summary panel
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "System Summary");
    HwInfo hw;
    platform_hw_info(&hw);
    char buf[96];
    bios_info_row(s, px + 12, py + 40, "CPU:", hw.cpu_model, BIOS_TXT);
    ksprintf(buf, sizeof(buf), "%u MHz x %u cores", hw.cpu_mhz, (unsigned)hw.cpu_cores);
    bios_info_row(s, px + 12, py + 62, "Speed:", buf, BIOS_TXT);
    uint32_t mu = 0, mt = 0;
    platform_mem_stats(&mu, &mt);
    ksprintf(buf, sizeof(buf), "%u MB (used %u MB)", (unsigned)(mt / 1048576), (unsigned)(mu / 1048576));
    bios_info_row(s, px + 12, py + 84, "Memory:", buf, BIOS_TXT);
    ksprintf(buf, sizeof(buf), "%s %s", hw.bios_vendor, hw.bios_version);
    bios_info_row(s, px + 12, py + 106, "BIOS:", buf, BIOS_TXT);
    DiskInfo disks[4];
    int nd = platform_disk_scan(disks, 4);
    if (nd > 0) {
        ksprintf(buf, sizeof(buf), "%s (%s)", disks[0].name, disks[0].model);
        bios_info_row(s, px + 12, py + 128, "Boot Disk:", buf, BIOS_TXT);
    }
    bios_info_row(s, px + 12, py + 150, "Boot Mode:", "UEFI", BIOS_OK);
    // bottom hints
    gfx::text(s, px + 12, py + ph - 34, "Press ENTER to enter a menu,", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + ph - 16, "F10 to save & exit setup.", BIOS_TXT_DIM, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    Enter: Select    ESC: Exit Setup",
                     "F10: Save & Exit    ←/→: Adjust values");
}

// ---- system overview (read-only) ----
static void bios_paint_system(Surface& s) {
    bios_title_bar(s, "System Overview");
    int x = 30, y = 56;
    HwInfo hw;
    platform_hw_info(&hw);
    char buf[96];

    // processor and memory
    gfx::text(s, x, y, "Processor", BIOS_AMBER, BIOS_BG);
    bios_info_row(s, x + 20, y + 20, "CPU Model:", hw.cpu_model, BIOS_TXT);
    ksprintf(buf, sizeof(buf), "%u MHz", hw.cpu_mhz);
    bios_info_row(s, x + 20, y + 40, "CPU Speed:", buf, BIOS_TXT);
    ksprintf(buf, sizeof(buf), "%u", (unsigned)hw.cpu_cores);
    bios_info_row(s, x + 20, y + 60, "CPU Cores:", buf, BIOS_TXT);
    bios_info_row(s, x + 20, y + 80, "CPU Features:", "SSE2 AVX", BIOS_TXT);
    uint32_t mu = 0, mt = 0;
    platform_mem_stats(&mu, &mt);
    ksprintf(buf, sizeof(buf), "%u MB", (unsigned)(mt / 1048576));
    bios_info_row(s, x + 20, y + 100, "Total Memory:", buf, BIOS_TXT);
    ksprintf(buf, sizeof(buf), "%u MB", (unsigned)(mu / 1048576));
    bios_info_row(s, x + 20, y + 120, "Memory Used:", buf, BIOS_WARN);

    // BIOS and firmware
    y += 154;
    gfx::text(s, x, y, "Firmware", BIOS_AMBER, BIOS_BG);
    ksprintf(buf, sizeof(buf), "%s %s", hw.bios_vendor, hw.bios_version);
    bios_info_row(s, x + 20, y + 20, "BIOS Version:", buf, BIOS_TXT);
    bios_info_row(s, x + 20, y + 40, "BIOS Date:", "09/23/2026", BIOS_TXT);
    bios_info_row(s, x + 20, y + 60, "Boot Mode:", "UEFI", BIOS_OK);
    // network interface (reads the real MAC)
    NetAdapterInfo net;
    if (platform_net_get(&net)) {
        ksprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 net.mac[0], net.mac[1], net.mac[2], net.mac[3], net.mac[4], net.mac[5]);
        bios_info_row(s, x + 20, y + 80, "MAC Address:", buf, BIOS_TXT);
    }
    bios_info_row(s, x + 20, y + 100, "Memory Type:", "DDR4-3200", BIOS_TXT);

    // time and uptime
    y += 134;
    DateInfo di;
    if (platform_rtc_date(&di)) {
        ksprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                 di.year, di.month, di.day, di.hour, di.min, di.sec);
        bios_info_row(s, x, y + 6, "System Date/Time:", buf, BIOS_TXT);
    }
    uint32_t up = nefuos_uptime_ms() / 1000;
    ksprintf(buf, sizeof(buf), "%02u:%02u:%02u", up / 3600, (up / 60) % 60, up % 60);
    bios_info_row(s, x, y + 24, "Uptime:", buf, BIOS_TXT);

    // disk information
    DiskInfo disks[4];
    int nd = platform_disk_scan(disks, 4);
    y += 62;
    gfx::text(s, x, y, "Storage", BIOS_AMBER, BIOS_BG);
    for (int i = 0; i < nd && i < 2; i++) {
        ksprintf(buf, sizeof(buf), "%s  %s  %u MB",
                 disks[i].name, disks[i].model,
                 (unsigned)(disks[i].sectors / 2048));
        bios_info_row(s, x + 20, y + 20 + i * 20, "Disk:", buf, BIOS_TXT);
    }

    bios_help_bar(s, "System information is read-only.", "ESC: Return to Main Menu");
}

// ---- boot settings ----
static void bios_paint_boot(Surface& s) {
    bios_title_bar(s, "Boot Settings");
    int x = 30, y = BIOS_MENU_TOP;
    int sel = s_bios.sel[PAGE_BOOT];
    char buf[48];

    // 0: boot timeout (seconds)
    ksprintf(buf, sizeof(buf), "%d sec", (int)s_work.boot_timeout);
    bios_draw_item(s, x, y, 300, "Boot Timeout", buf, sel == 0, false, "[←/→]");

    // 1: boot splash
    bios_draw_item(s, x, y + BIOS_ROW_H, 300, "Boot Splash",
                   s_work.boot_splash ? "Enabled" : "Disabled",
                   sel == 1, false, "[←/→]");

    // 2: boot wallpaper
    bios_draw_item(s, x, y + 2 * BIOS_ROW_H, 300, "Boot Wallpaper",
                   s_work.wallpaper_boot[0] ? s_work.wallpaper_boot : "blue",
                   sel == 2, false, "[←/→]");

    // 3: lock wallpaper
    bios_draw_item(s, x, y + 3 * BIOS_ROW_H, 300, "Lock Wallpaper",
                   s_work.wallpaper_lock[0] ? s_work.wallpaper_lock : "dark",
                   sel == 3, false, "[←/→]");

    // 4: boot mode (read-only)
    bios_draw_item(s, x, y + 4 * BIOS_ROW_H, 300, "Boot Mode", "UEFI",
                   sel == 4, false, 0);

    // 5: boot order: custom entries (multi-boot) first, otherwise the disk list
    DiskInfo disks[4];
    int nd = platform_disk_scan(disks, 4);
    if (s_work.boot_entry_count > 0) {
        int shown = s_work.boot_entry_count > 3 ? 3 : s_work.boot_entry_count;
        int pos = 0;
        for (int i = 0; i < shown; i++) {
            pos += ksprintf(buf + pos, sizeof(buf) - (size_t)pos, "%d.%s ", i + 1, s_work.boot_entries[i].name);
        }
        if (s_work.boot_entry_count > 3) {
            pos += ksprintf(buf + pos, sizeof(buf) - (size_t)pos, "+%d", s_work.boot_entry_count - 3);
        }
        buf[sizeof(buf) - 1] = 0;
    } else if (nd > 0) {
        int bf = s_bios.boot_first % nd;
        if (nd > 1) {
            int bf2 = (bf + 1) % nd;
            ksprintf(buf, sizeof(buf), "1.%s 2.%s", disks[bf].name, disks[bf2].name);
        } else {
            ksprintf(buf, sizeof(buf), "1.%s", disks[bf].name);
        }
    } else {
        strncpy(buf, "1. HDD-0", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
    }
    bios_draw_item(s, x, y + 5 * BIOS_ROW_H, 300, "Boot Order", buf,
                   sel == 5, false, "[←/→]");

    // 6: add boot entry (multi / dual boot)
    bios_draw_item(s, x, y + 6 * BIOS_ROW_H, 300, "Add Boot Entry",
                   s_work.boot_entry_count >= NEFU_MAX_BOOT_ENTRIES ? "(full)" : "",
                   sel == 6, false, "[Enter]");

    // 7: remove boot entry
    bios_draw_item(s, x, y + 7 * BIOS_ROW_H, 300, "Remove Boot Entry",
                   s_work.boot_entry_count == 0 ? "(none)" : "",
                   sel == 7, false, "[Enter]");

    // right help panel
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "Boot Help");
    gfx::text(s, px + 12, py + 40, "Boot Timeout : seconds to wait", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 60, "  before booting the OS.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 88, "Boot Splash  : show the nefuOS", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 108, "  boot animation on startup.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 136, "Boot Entries : custom boot list", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 156, "  for dual / multi OS boot.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 184, "Add / Remove : manage the list.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 204, "  The first entry boots first.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 232, "Use ←/→ or +/- to change a", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 252, "value. Changes take effect", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 272, "after Save & Exit.", BIOS_WARN, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    ←/→: Adjust    ENTER: Toggle / Enter",
                     "ESC: Return to Main Menu    F10: Save & Exit");
}

// ---- add boot entry (multi-boot) ----
static void bios_paint_boot_add(Surface& s) {
    bios_title_bar(s, "Add Boot Entry");
    int x = 30, y = BIOS_MENU_TOP;
    int sel = s_bios.sel[PAGE_BOOT_ADD];
    char buf[64];

    // device list: all disks + a final "nefuOS (this system)" entry
    DiskInfo disks[4];
    int nd = platform_disk_scan(disks, 4);
    int dev_count = nd + 1;
    if (s_bios.boot_add_dev >= dev_count) s_bios.boot_add_dev = 0;

    // 0: name (editable)
    bool editing_name = (s_bios.edit == EDIT_BOOT_NAME);
    bios_draw_item(s, x, y, 300, "Name",
                   editing_name ? s_bios.edit_buf : (s_bios.boot_add_name[0] ? s_bios.boot_add_name : "(auto)"),
                   sel == 0, editing_name, "[Enter]");

    // 1: target device (left/right to switch)
    if (s_bios.boot_add_dev < nd)
        ksprintf(buf, sizeof(buf), "%s (%s)", disks[s_bios.boot_add_dev].name, disks[s_bios.boot_add_dev].model);
    else
        strncpy(buf, "nefuOS (this system)", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    bios_draw_item(s, x, y + BIOS_ROW_H, 300, "Target Device", buf,
                   sel == 1, false, "[←/→]");

    // 2: boot type (left/right to switch)
    static const char* k_kinds[] = { "Operating System", "Boot from Disk", "Boot from ISO" };
    bios_draw_item(s, x, y + 2 * BIOS_ROW_H, 300, "Boot Type",
                   k_kinds[s_bios.boot_add_kind % 3], sel == 2, false, "[←/→]");

    // 3: confirm add
    bios_draw_item(s, x, y + 3 * BIOS_ROW_H, 300, "Add Boot Entry", 0,
                   sel == 3, false, "[Enter]");

    // 4: cancel
    bios_draw_item(s, x, y + 4 * BIOS_ROW_H, 300, "Cancel", 0,
                   sel == 4, false, "[Enter]");

    // right help panel
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "Boot Entry Help");
    gfx::text(s, px + 12, py + 40, "Add an OS / disk to the boot", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 60, "order for dual / multi system.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 88, "Name    : shown in Boot Order.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 108, "Device  : the drive to boot,", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 128, "  or 'nefuOS' for this system.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 156, "Type    : OS / Disk / ISO.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 184, "The first entry becomes the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 204, "default boot target.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 240, "Max 8 entries. Save with F10.", BIOS_WARN, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    ←/→: Adjust    ENTER: Edit / Add",
                     "ESC: Back to Boot Settings    F10: Save & Exit");
}

// ---- remove boot entry ----
static void bios_paint_boot_del(Surface& s) {
    bios_title_bar(s, "Remove Boot Entry");
    int x = 30, y = BIOS_MENU_TOP;
    int n = s_work.boot_entry_count;
    if (n <= 0) {
        gfx::text(s, x, y, "No custom boot entries.", BIOS_TXT_DIM, BIOS_BG);
        bios_help_bar(s, "ESC: Back to Boot Settings", "F10: Save & Exit");
        return;
    }
    int sel = s_bios.sel[PAGE_BOOT_DEL];
    if (sel >= n) sel = n - 1;
    char buf[64];
    for (int i = 0; i < n; i++) {
        ksprintf(buf, sizeof(buf), "%d. %s  (%s, %s)", i + 1,
                 s_work.boot_entries[i].name, s_work.boot_entries[i].device,
                 s_work.boot_entries[i].kind);
        bios_draw_item(s, x, y + i * BIOS_ROW_H, 300, buf, 0,
                       i == sel, false, i == sel ? "[Enter: remove]" : 0);
    }
    // right help panel
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "Remove Help");
    gfx::text(s, px + 12, py + 40, "Select an entry and press", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 60, "ENTER to remove it from the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 80, "boot order.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 108, "Changes take effect after", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 128, "Save & Exit.", BIOS_WARN, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    ENTER: Remove    ESC: Back",
                     "F10: Save & Exit");
}

// ---- security settings ----
static void bios_paint_security(Surface& s) {
    bios_title_bar(s, "Security Settings");
    int x = 30, y = BIOS_MENU_TOP;
    int sel = s_bios.sel[PAGE_SECURITY];

    // 0: username (editable)
    bool editing_user = (s_bios.edit == EDIT_USERNAME);
    bios_draw_item(s, x, y, 260, "System Username",
                   editing_user ? s_bios.edit_buf : (s_work.username[0] ? s_work.username : "(empty)"),
                   sel == 0, editing_user, "[Enter]");

    // 1: set new password
    bool editing_p1 = (s_bios.edit == EDIT_PWD_NEW);
    bios_draw_item(s, x, y + BIOS_ROW_H, 260, "Set Password",
                   editing_p1 ? s_bios.edit_buf : (s_work.password_hash[0] ? "********" : "(not set)"),
                   sel == 1, editing_p1, "[Enter]");

    // 2: confirm password
    bool editing_p2 = (s_bios.edit == EDIT_PWD_CONFIRM);
    bios_draw_item(s, x, y + 2 * BIOS_ROW_H, 260, "Confirm Password",
                   editing_p2 ? s_bios.edit_buf : "(re-enter new password)",
                   sel == 2, editing_p2, "[Enter]");

    // 3: clear password
    bios_draw_item(s, x, y + 3 * BIOS_ROW_H, 260, "Clear Password",
                   0, sel == 3, false, "[Enter]");

    // 4: password level (read-only)
    char buf[48];
    if (s_work.password_hash[0]) {
        int len = (int)strlen(s_work.password_hash);
        strncpy(buf, (len >= 40) ? "Strong" : (len >= 20 ? "Medium" : "Weak"), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
    } else {
        strncpy(buf, "None", sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
    }
    bios_draw_item(s, x, y + 4 * BIOS_ROW_H, 260, "Password Level", buf,
                   sel == 4, false, 0);

    // right help panel
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "Security Help");
    gfx::text(s, px + 12, py + 40, "Username is used on the lock", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 60, "screen and the welcome page.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 88, "Password protects access to", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 108, "nefuOS. It is stored as a", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 128, "SHA-256 hash, never in plain.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 156, "Press ENTER on a field to", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 176, "edit it. Password needs to be", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 196, "entered twice to confirm.", BIOS_TXT_DIM, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    ENTER: Edit/Confirm    Backspace: Delete",
                     "ESC: Return to Main Menu    F10: Save & Exit");
}

// ---- power management ----
static void bios_paint_power(Surface& s) {
    bios_title_bar(s, "Power Management");
    int x = 30, y = BIOS_MENU_TOP;
    int sel = s_bios.sel[PAGE_POWER];
    char buf[48];

    // 0: lock on suspend (toggle)
    bios_draw_item(s, x, y, 300, "Lock on Suspend",
                   s_bios.lock_on_suspend ? "Enabled" : "Disabled",
                   sel == 0, false, "[←/→]");

    // 1: idle auto-lock timeout
    if (s_bios.idle_lock_sec == 0)
        strncpy(buf, "Disabled", sizeof(buf) - 1);
    else
        ksprintf(buf, sizeof(buf), "%d sec", s_bios.idle_lock_sec);
    buf[sizeof(buf) - 1] = 0;
    bios_draw_item(s, x, y + BIOS_ROW_H, 300, "Idle Auto-Lock", buf,
                   sel == 1, false, "[←/→]");

    // 2: suspend mode (read-only)
    bios_draw_item(s, x, y + 2 * BIOS_ROW_H, 300, "Suspend Mode", "S3 (RAM)",
                   sel == 2, false, 0);

    // 3: CPU power states (read-only)
    bios_draw_item(s, x, y + 3 * BIOS_ROW_H, 300, "CPU Power States", "C0-C6",
                   sel == 3, false, 0);

    // 4: fan control (read-only)
    bios_draw_item(s, x, y + 4 * BIOS_ROW_H, 300, "Fan Control", "Smart",
                   sel == 4, false, 0);

    // 5: AC power status (read-only)
    bios_draw_item(s, x, y + 5 * BIOS_ROW_H, 300, "AC Power Status", "Plugged In",
                   sel == 5, false, 0);

    // right help panel
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "Power Help");
    gfx::text(s, px + 12, py + 40, "Lock on Suspend : lock the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 60, "  screen when the system", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 80, "  enters suspend.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 108, "Idle Auto-Lock  : lock after", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 128, "  the given idle time.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 156, "Other rows report the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 176, "current power profile of", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 196, "the virtual machine.", BIOS_TXT_DIM, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    ←/→: Adjust    ENTER: Toggle",
                     "ESC: Return to Main Menu    F10: Save & Exit");
}

// ---- exit menu ----
static void bios_paint_exit(Surface& s) {
    bios_title_bar(s, "Exit Menu");
    int x = 30, y = BIOS_MENU_TOP;
    int sel = s_bios.sel[PAGE_EXIT];

    const char* items[] = {
        "Save Changes and Exit",
        "Discard Changes and Exit",
        "Load Setup Defaults",
        "Save Changes",
        "Discard Changes"
    };
    for (int i = 0; i < 5; i++) {
        bios_draw_item(s, x, y + i * BIOS_ROW_H, 260, items[i], 0, i == sel, false, "[Enter]");
    }

    // right: current status
    int px = 420, py = BIOS_MENU_TOP - 6, pw = s.width - px - 20, ph = s.height - py - 76;
    bios_panel(s, px, py, pw, ph, "Exit Summary");
    gfx::text(s, px + 12, py + 40, "Unsaved changes:", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 60, s_bios.dirty ? "YES" : "no", s_bios.dirty ? BIOS_WARN : BIOS_OK, BIOS_BG);
    gfx::text(s, px + 12, py + 96, "Save Changes : write the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 116, "  working copy to the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 136, "  system and stay here.", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 164, "Load Defaults : restore the", BIOS_TXT_DIM, BIOS_BG);
    gfx::text(s, px + 12, py + 184, "  factory BIOS defaults.", BIOS_TXT_DIM, BIOS_BG);

    bios_help_bar(s, "↑↓: Move    ENTER: Execute    ESC: Return to Main Menu",
                     "F10: Save & Exit");
}

// ---- confirm dialog ----
static void bios_paint_confirm(Surface& s) {
    // centered grey-blue dialog
    int W = s.width, H = s.height;
    int dw = 440, dh = 150;
    int dx = (W - dw) / 2, dy = (H - dh) / 2 - 20;
    gfx::fillrect(s, dx, dy, dw, dh, 0x00005588);
    gfx::rect(s, dx, dy, dw, dh, BIOS_TXT);
    gfx::rect(s, dx + 2, dy + 2, dw - 4, dh - 4, BIOS_TXT_DIM);

    const char* title = "Confirm";
    const char* ask = "";
    switch (s_bios.confirm) {
    case CF_EXIT_SAVE:    title = "Save & Exit";    ask = "Save changes and exit setup?"; break;
    case CF_EXIT_DISCARD: title = "Exit Without Saving"; ask = "Discard changes and exit setup?"; break;
    case CF_CLEAR_PWD:    title = "Clear Password"; ask = "Remove the current password?"; break;
    case CF_LOAD_DEFAULTS:title = "Load Defaults";  ask = "Restore factory defaults?"; break;
    default: break;
    }
    gfx::text(s, dx + 20, dy + 16, title, BIOS_AMBER, 0x00005588);
    gfx::hline(s, dx + 10, dx + dw - 10, dy + 36, BIOS_TXT_DIM);
    gfx::text(s, dx + 20, dy + 60, ask, BIOS_TXT, 0x00005588);
    gfx::text(s, dx + 20, dy + 96, "Y / ENTER : Yes      N / ESC : No",
              BIOS_TXT_DIM, 0x00005588);
}

// ==================== Page dispatch rendering ====================
static void bios_paint(Surface& s) {
    switch (s_bios.page) {
    case PAGE_MAIN:     bios_paint_main(s);        break;
    case PAGE_SYSTEM:   bios_paint_system(s);      break;
    case PAGE_BOOT:     bios_paint_boot(s);        break;
    case PAGE_BOOT_ADD: bios_paint_boot_add(s);    break;
    case PAGE_BOOT_DEL: bios_paint_boot_del(s);    break;
    case PAGE_SECURITY: bios_paint_security(s);    break;
    case PAGE_POWER:    bios_paint_power(s);       break;
    case PAGE_EXIT:     bios_paint_exit(s);        break;
    default: break;
    }
    // the confirm dialog is drawn on top
    if (s_bios.confirm != CF_NONE) bios_paint_confirm(s);
    // the tab strip is always drawn last (on top)
    bios_tab_strip(s);
}

// ==================== Save / discard / defaults ====================

// apply the working copy to the system (core of Save)
static void bios_apply() {
    // UEFI config: username / password hash / boot timeout / splash / wallpapers / boot entries
    g_uefi = s_work;
    g_uefi.boot_first = s_bios.boot_first;
    // sync desktop settings
    g_settings.lock_on_suspend = s_bios.lock_on_suspend;
    g_settings.idle_lock_sec = s_bios.idle_lock_sec;
    g_settings.boot_splash = s_work.boot_splash;
    int bi = bios_wallpaper_index(s_work.wallpaper_boot);
    if (bi >= 0) g_settings.wallpaper_boot = bi;
    int li = bios_wallpaper_index(s_work.wallpaper_lock);
    if (li >= 0) g_settings.wallpaper_lock = li;
    // platform layer (no-op on host, CMOS write on bare) and persist the VFS config
    platform_uefi_save(&g_uefi);
    settings_save();
    s_bios.dirty = false;
}

// drop the working copy and reload from the system
static void bios_discard() {
    s_work = g_uefi;
    s_bios.lock_on_suspend = g_settings.lock_on_suspend;
    s_bios.idle_lock_sec = g_settings.idle_lock_sec;
    s_bios.dirty = false;
}

// restore factory defaults (modifies the working copy only)
static void bios_load_defaults() {
    memset(&s_work, 0, sizeof(s_work));
    strncpy(s_work.username, "user", sizeof(s_work.username) - 1);
    s_work.boot_timeout = 5;
    s_work.boot_splash = true;
    strncpy(s_work.wallpaper_boot, "blue", sizeof(s_work.wallpaper_boot) - 1);
    strncpy(s_work.wallpaper_lock, "dark", sizeof(s_work.wallpaper_lock) - 1);
    s_bios.lock_on_suspend = true;
    s_bios.idle_lock_sec = 0;
    s_bios.boot_first = 0;
    bios_mark_dirty();
    bios_set_msg("Setup defaults loaded. Save to apply.");
}

// YES / NO handling of the confirm dialog
static void bios_confirm_yes(Window* w) {
    switch (s_bios.confirm) {
    case CF_EXIT_SAVE:
        bios_apply();
        printf("[BIOS] Settings saved, exiting setup.\n");
        s_bios.running = false;
        if (w) g_wm->close_window(w);
        break;
    case CF_EXIT_DISCARD:
        bios_discard();
        printf("[BIOS] Exiting setup without saving.\n");
        s_bios.running = false;
        if (w) g_wm->close_window(w);
        break;
    case CF_CLEAR_PWD:
        s_work.password_hash[0] = 0;
        bios_mark_dirty();
        bios_set_msg("Password cleared. Save to apply.");
        break;
    case CF_LOAD_DEFAULTS:
        bios_load_defaults();
        break;
    default: break;
    }
    s_bios.confirm = CF_NONE;
}

static void bios_confirm_no() {
    s_bios.confirm = CF_NONE;
}

// ==================== Inline editing ====================

// begin editing: copy the current value into the buffer
static void bios_edit_begin(BiosEdit mode) {
    s_bios.edit = mode;
    s_bios.edit_len = 0;
    if (mode == EDIT_USERNAME) {
        strncpy(s_bios.edit_buf, s_work.username, sizeof(s_bios.edit_buf) - 1);
        s_bios.edit_buf[sizeof(s_bios.edit_buf) - 1] = 0;
        s_bios.edit_len = (int)strlen(s_bios.edit_buf);
    } else if (mode == EDIT_BOOT_NAME) {
        strncpy(s_bios.edit_buf, s_bios.boot_add_name, sizeof(s_bios.edit_buf) - 1);
        s_bios.edit_buf[sizeof(s_bios.edit_buf) - 1] = 0;
        s_bios.edit_len = (int)strlen(s_bios.edit_buf);
    }
}

// editing done: commit according to the mode
static void bios_edit_commit() {
    BiosEdit mode = s_bios.edit;
    s_bios.edit_buf[sizeof(s_bios.edit_buf) - 1] = 0;
    if (mode == EDIT_USERNAME) {
        // strip leading/trailing spaces and the forbidden '=' (config files use = as key/value separator)
        int i = 0, j = 0;
        while (s_bios.edit_buf[i] == ' ') i++;
        for (; s_bios.edit_buf[i]; i++) {
            if (s_bios.edit_buf[i] != '=') s_bios.edit_buf[j++] = s_bios.edit_buf[i];
        }
        s_bios.edit_buf[j] = 0;
        while (j > 0 && s_bios.edit_buf[j - 1] == ' ') s_bios.edit_buf[--j] = 0;
        if (j > 0) {
            strncpy(s_work.username, s_bios.edit_buf, sizeof(s_work.username) - 1);
            s_work.username[sizeof(s_work.username) - 1] = 0;
            bios_mark_dirty();
            bios_set_msg("Username updated. Save to apply.");
        } else {
            bios_set_msg("Username cannot be empty!");
        }
    } else if (mode == EDIT_PWD_NEW) {
        if (s_bios.edit_len == 0) {
            s_bios.edit = EDIT_NONE;
            bios_set_msg("Password unchanged.");
            return;
        }
        // stage the new password and enter the confirm phase
        strncpy(s_new_pwd, s_bios.edit_buf, sizeof(s_new_pwd) - 1);
        s_new_pwd[sizeof(s_new_pwd) - 1] = 0;
        s_bios.edit = EDIT_PWD_CONFIRM;
        s_bios.edit_len = 0;
        s_bios.edit_buf[0] = 0;
        bios_set_msg("Re-enter the new password to confirm.");
        return;
    } else if (mode == EDIT_PWD_CONFIRM) {
        s_bios.edit = EDIT_NONE;
        if (strcmp(s_bios.edit_buf, s_new_pwd) == 0 && s_bios.edit_len > 0) {
            // both match -> hash with SHA-256 and write into the working copy
            uint8_t d[32];
            nefu_sha256((const uint8_t*)s_new_pwd, (uint32_t)strlen(s_new_pwd), d);
            char hex[65];
            nefu_sha256_hex(d, hex);
            strncpy(s_work.password_hash, hex, sizeof(s_work.password_hash) - 1);
            s_work.password_hash[sizeof(s_work.password_hash) - 1] = 0;
            bios_mark_dirty();
            bios_set_msg("Password updated. Save to apply.");
        } else {
            bios_set_msg("Passwords do not match!");
        }
        s_new_pwd[0] = 0;
        s_bios.edit_len = 0;
        s_bios.edit_buf[0] = 0;
        return;
    } else if (mode == EDIT_BOOT_NAME) {
        // boot entry name: strip leading/trailing spaces and '=' (config separator)
        int i = 0, j = 0;
        while (s_bios.edit_buf[i] == ' ') i++;
        for (; s_bios.edit_buf[i]; i++) {
            if (s_bios.edit_buf[i] != '=') s_bios.edit_buf[j++] = s_bios.edit_buf[i];
        }
        s_bios.edit_buf[j] = 0;
        while (j > 0 && s_bios.edit_buf[j - 1] == ' ') s_bios.edit_buf[--j] = 0;
        if (j > 0) {
            strncpy(s_bios.boot_add_name, s_bios.edit_buf, sizeof(s_bios.boot_add_name) - 1);
            s_bios.boot_add_name[sizeof(s_bios.boot_add_name) - 1] = 0;
            bios_set_msg("Boot entry name set.");
        } else {
            s_bios.boot_add_name[0] = 0;
            bios_set_msg("Name left empty: auto name will be used.");
        }
    }
    s_bios.edit = EDIT_NONE;
}

// ==================== Keyboard handling ====================

// commit "add boot entry": write the sub-page state into the working copy's boot entry list
static void bios_boot_add_commit() {
    if (s_work.boot_entry_count >= NEFU_MAX_BOOT_ENTRIES) {
        bios_set_msg("Boot entry list is full (max 8). Remove one first.");
        return;
    }
    DiskInfo dk[4];
    int ndk = platform_disk_scan(dk, 4);
    UefiBootEntry* e = &s_work.boot_entries[s_work.boot_entry_count];
    // name: prefer the edited name, otherwise auto-generate from the device
    if (s_bios.boot_add_name[0]) {
        strncpy(e->name, s_bios.boot_add_name, sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = 0;
    } else if (s_bios.boot_add_dev < ndk) {
        ksprintf(e->name, sizeof(e->name), "OS on %s", dk[s_bios.boot_add_dev].name);
    } else {
        strncpy(e->name, "nefuOS", sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = 0;
    }
    // device: disk name or "nefuOS"
    if (s_bios.boot_add_dev < ndk) {
        strncpy(e->device, dk[s_bios.boot_add_dev].name, sizeof(e->device) - 1);
        e->device[sizeof(e->device) - 1] = 0;
    } else {
        strncpy(e->device, "nefuOS", sizeof(e->device) - 1);
        e->device[sizeof(e->device) - 1] = 0;
    }
    // kind tag
    static const char* k_tags[] = { "OS", "Disk", "ISO" };
    strncpy(e->kind, k_tags[s_bios.boot_add_kind % 3], sizeof(e->kind) - 1);
    e->kind[sizeof(e->kind) - 1] = 0;

    s_work.boot_entry_count++;
    // reset the sub-page state and return to the Boot page
    s_bios.boot_add_name[0] = 0;
    s_bios.boot_add_dev = 0;
    s_bios.boot_add_kind = 0;
    s_bios.sel[PAGE_BOOT] = 5;      // land on the Boot Order row so the new entry is immediately visible
    s_bios.page = PAGE_BOOT;
    bios_mark_dirty();
    bios_set_msg("Boot entry added. Save to apply.");
}

// common entry for page navigation / adjustment actions
static void bios_handle_page_key(Window* w, const KeyEvent* e) {
    (void)w;   // page-key handling needs no window pointer (exits go through the confirm dialog)
    int kc = e->keycode;
    char ch = e->ascii;
    bool down = e->down;
    if (!down) return;

    // global hotkey: F10 saves and exits (except while editing / confirming)
    if (kc == KEY_F10 && s_bios.confirm == CF_NONE && s_bios.edit == EDIT_NONE) {
        s_bios.confirm = CF_EXIT_SAVE;
        return;
    }

    switch (s_bios.page) {
    case PAGE_MAIN: {
        int n = 6, sel = s_bios.sel[PAGE_MAIN];
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_MAIN] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_MAIN] = sel; }
        else if (kc == KEY_ENTER || kc == KEY_SPACE) {
            switch (sel) {
            case 0: s_bios.page = PAGE_SYSTEM;   s_bios.sel[PAGE_SYSTEM] = 0; break;
            case 1: s_bios.page = PAGE_BOOT;     s_bios.sel[PAGE_BOOT] = 0; break;
            case 2: s_bios.page = PAGE_SECURITY; s_bios.sel[PAGE_SECURITY] = 0; break;
            case 3: s_bios.page = PAGE_POWER;    s_bios.sel[PAGE_POWER] = 0; break;
            case 4: s_bios.confirm = CF_EXIT_SAVE; break;
            case 5: s_bios.confirm = CF_EXIT_DISCARD; break;
            default: break;
            }
        } else if (kc == KEY_ESC) {
            s_bios.confirm = CF_EXIT_DISCARD;
        }
        break;
    }
    case PAGE_SYSTEM:
        if (kc == KEY_ESC || kc == KEY_LEFT) s_bios.page = PAGE_MAIN;
        break;

    case PAGE_BOOT: {
        int n = 8, sel = s_bios.sel[PAGE_BOOT];
        bool plus = (kc == KEY_RIGHT || ch == '+' || ch == '=');
        bool minus = (kc == KEY_LEFT || ch == '-' || ch == '_');
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_BOOT] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_BOOT] = sel; }
        else if (kc == KEY_ESC) { s_bios.page = PAGE_MAIN; }
        else if (plus || minus || kc == KEY_ENTER || kc == KEY_SPACE) {
            switch (sel) {
            case 0: { // Boot Timeout 0..30 seconds
                int v = (int)s_work.boot_timeout;
                if (plus) v = (v + 1 <= 30) ? v + 1 : 30;
                else if (minus) v = (v - 1 >= 0) ? v - 1 : 0;
                else if (kc == KEY_ENTER || kc == KEY_SPACE) v = (v == 0) ? 5 : 0;
                s_work.boot_timeout = (uint8_t)v;
                bios_mark_dirty();
                break;
            }
            case 1: // Boot Splash toggle
                if (plus || minus || kc == KEY_ENTER || kc == KEY_SPACE) {
                    s_work.boot_splash = !s_work.boot_splash;
                    bios_mark_dirty();
                }
                break;
            case 2: { // cycle boot wallpaper
                int idx = bios_wallpaper_index(s_work.wallpaper_boot);
                if (idx < 0) idx = 0;
                if (minus) idx = (idx + k_wallpaper_count - 1) % k_wallpaper_count;
                else idx = (idx + 1) % k_wallpaper_count;
                strncpy(s_work.wallpaper_boot, k_wallpapers[idx], sizeof(s_work.wallpaper_boot) - 1);
                s_work.wallpaper_boot[sizeof(s_work.wallpaper_boot) - 1] = 0;
                bios_mark_dirty();
                break;
            }
            case 3: { // cycle lock wallpaper
                int idx = bios_wallpaper_index(s_work.wallpaper_lock);
                if (idx < 0) idx = 1;
                if (minus) idx = (idx + k_wallpaper_count - 1) % k_wallpaper_count;
                else idx = (idx + 1) % k_wallpaper_count;
                strncpy(s_work.wallpaper_lock, k_wallpapers[idx], sizeof(s_work.wallpaper_lock) - 1);
                s_work.wallpaper_lock[sizeof(s_work.wallpaper_lock) - 1] = 0;
                bios_mark_dirty();
                break;
            }
            case 5: { // boot order: rotate the first custom entry if any, otherwise rotate disks
                if (s_work.boot_entry_count > 1) {
                    // move entry 0 to the end = switch the first boot target
                    UefiBootEntry tmp = s_work.boot_entries[0];
                    for (int i = 0; i < s_work.boot_entry_count - 1; i++)
                        s_work.boot_entries[i] = s_work.boot_entries[i + 1];
                    s_work.boot_entries[s_work.boot_entry_count - 1] = tmp;
                    bios_mark_dirty();
                } else if (s_work.boot_entry_count == 1) {
                    bios_set_msg("Only one boot entry. Add more to reorder.");
                } else {
                    DiskInfo dk[4];
                    int ndk = platform_disk_scan(dk, 4);
                    if (ndk > 1) {
                        int bf = s_bios.boot_first;
                        if (minus) bf = (bf + ndk - 1) % ndk;
                        else bf = (bf + 1) % ndk;
                        s_bios.boot_first = bf;
                        bios_mark_dirty();
                    } else {
                        bios_set_msg("Only one boot device detected.");
                    }
                }
                break;
            }
            case 6: // enter the add-boot-entry sub-page
                if (kc == KEY_ENTER || kc == KEY_SPACE) {
                    if (s_work.boot_entry_count >= NEFU_MAX_BOOT_ENTRIES) {
                        bios_set_msg("Boot entry list is full (max 8).");
                    } else {
                        s_bios.page = PAGE_BOOT_ADD;
                        s_bios.sel[PAGE_BOOT_ADD] = 0;
                        s_bios.boot_add_dev = 0;
                        s_bios.boot_add_kind = 0;
                        s_bios.boot_add_name[0] = 0;
                    }
                }
                break;
            case 7: // enter the remove-boot-entry sub-page
                if (kc == KEY_ENTER || kc == KEY_SPACE) {
                    if (s_work.boot_entry_count == 0) {
                        bios_set_msg("No custom boot entries to remove.");
                    } else {
                        s_bios.page = PAGE_BOOT_DEL;
                        s_bios.sel[PAGE_BOOT_DEL] = 0;
                    }
                }
                break;
            default: break; // read-only rows (Boot Mode etc.)
            }
        }
        break;
    }

    case PAGE_BOOT_ADD: {
        int n = 5, sel = s_bios.sel[PAGE_BOOT_ADD];
        bool plus = (kc == KEY_RIGHT || ch == '+' || ch == '=');
        bool minus = (kc == KEY_LEFT || ch == '-' || ch == '_');
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_BOOT_ADD] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_BOOT_ADD] = sel; }
        else if (kc == KEY_ESC) { s_bios.page = PAGE_BOOT; }
        else if (kc == KEY_ENTER || kc == KEY_SPACE) {
            switch (sel) {
            case 0: bios_edit_begin(EDIT_BOOT_NAME); break;   // edit name
            case 3: bios_boot_add_commit(); break;            // confirm add
            case 4: s_bios.page = PAGE_BOOT; break;           // cancel
            default: break;
            }
        } else if (plus || minus) {
            switch (sel) {
            case 1: { // target device: disk list + "nefuOS (this system)"
                DiskInfo dk[4];
                int ndk = platform_disk_scan(dk, 4);
                int dev_count = ndk + 1;
                int v = s_bios.boot_add_dev;
                if (minus) v = (v + dev_count - 1) % dev_count;
                else v = (v + 1) % dev_count;
                s_bios.boot_add_dev = v;
                break;
            }
            case 2: // boot type: OS / Disk / ISO
                if (minus) s_bios.boot_add_kind = (s_bios.boot_add_kind + 2) % 3;
                else s_bios.boot_add_kind = (s_bios.boot_add_kind + 1) % 3;
                break;
            default: break;
            }
        }
        break;
    }

    case PAGE_BOOT_DEL: {
        int n = s_work.boot_entry_count;
        if (n <= 0) { s_bios.page = PAGE_BOOT; break; }
        int sel = s_bios.sel[PAGE_BOOT_DEL];
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_BOOT_DEL] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_BOOT_DEL] = sel; }
        else if (kc == KEY_ESC || kc == KEY_LEFT) { s_bios.page = PAGE_BOOT; }
        else if (kc == KEY_ENTER || kc == KEY_DEL || kc == KEY_BACKSPACE) {
            // remove the selected entry: shift the array left
            for (int i = sel; i < n - 1; i++)
                s_work.boot_entries[i] = s_work.boot_entries[i + 1];
            s_work.boot_entry_count--;
            s_bios.sel[PAGE_BOOT_DEL] = 0;
            bios_mark_dirty();
            bios_set_msg("Boot entry removed. Save to apply.");
        }
        break;
    }

    case PAGE_SECURITY: {
        int n = 5, sel = s_bios.sel[PAGE_SECURITY];
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_SECURITY] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_SECURITY] = sel; }
        else if (kc == KEY_ESC) { s_bios.page = PAGE_MAIN; }
        else if (kc == KEY_ENTER || kc == KEY_SPACE) {
            switch (sel) {
            case 0: bios_edit_begin(EDIT_USERNAME); break;
            case 1: bios_edit_begin(EDIT_PWD_NEW); break;
            case 2:
                if (s_bios.edit == EDIT_PWD_NEW) {
                    // plain Enter confirms the first step and moves to the second
                    bios_edit_commit();
                }
                break;
            case 3:
                if (s_work.password_hash[0]) s_bios.confirm = CF_CLEAR_PWD;
                else bios_set_msg("No password is set.");
                break;
            default: break;
            }
        }
        break;
    }

    case PAGE_POWER: {
        int n = 6, sel = s_bios.sel[PAGE_POWER];
        bool plus = (kc == KEY_RIGHT || ch == '+' || ch == '=');
        bool minus = (kc == KEY_LEFT || ch == '-' || ch == '_');
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_POWER] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_POWER] = sel; }
        else if (kc == KEY_ESC) { s_bios.page = PAGE_MAIN; }
        else if (plus || minus || kc == KEY_ENTER || kc == KEY_SPACE) {
            switch (sel) {
            case 0: // Lock on Suspend
                s_bios.lock_on_suspend = !s_bios.lock_on_suspend;
                bios_mark_dirty();
                break;
            case 1: { // Idle Auto-Lock 0..120 seconds (0 = off)
                int v = s_bios.idle_lock_sec;
                if (plus) v = (v + 10 <= 120) ? v + 10 : 120;
                else if (minus) v = (v - 10 >= 0) ? v - 10 : 0;
                else if (kc == KEY_ENTER || kc == KEY_SPACE) v = (v == 0) ? 60 : 0;
                s_bios.idle_lock_sec = v;
                bios_mark_dirty();
                break;
            }
            default: break; // read-only rows
            }
        }
        break;
    }

    case PAGE_EXIT: {
        int n = 5, sel = s_bios.sel[PAGE_EXIT];
        if (kc == KEY_UP) { sel = (sel + n - 1) % n; s_bios.sel[PAGE_EXIT] = sel; }
        else if (kc == KEY_DOWN) { sel = (sel + 1) % n; s_bios.sel[PAGE_EXIT] = sel; }
        else if (kc == KEY_ESC) { s_bios.page = PAGE_MAIN; }
        else if (kc == KEY_ENTER || kc == KEY_SPACE) {
            switch (sel) {
            case 0: s_bios.confirm = CF_EXIT_SAVE; break;
            case 1: s_bios.confirm = CF_EXIT_DISCARD; break;
            case 2: s_bios.confirm = CF_LOAD_DEFAULTS; break;
            case 3: bios_apply();  bios_set_msg("Settings saved."); break;
            case 4: bios_discard(); bios_set_msg("Changes discarded."); break;
            default: break;
            }
        }
        break;
    }
    default: break;
    }
}

// unified BIOS keyboard entry: confirm dialog -> inline editing -> page
static void bios_on_key(Window* w, const KeyEvent* e) {
    if (!e || !e->down) return;

    // 1) the confirm dialog takes priority
    if (s_bios.confirm != CF_NONE) {
        int kc = e->keycode;
        char ch = e->ascii;
        if (kc == KEY_ENTER || ch == 'y' || ch == 'Y') bios_confirm_yes(w);
        else if (kc == KEY_ESC || ch == 'n' || ch == 'N') bios_confirm_no();
        return;
    }

    // 2) inline editing
    if (s_bios.edit != EDIT_NONE) {
        int kc = e->keycode;
        char ch = e->ascii;
        if (kc == KEY_ESC) {
            s_bios.edit = EDIT_NONE;   // cancel editing
            s_new_pwd[0] = 0;
            return;
        }
        if (kc == KEY_ENTER) {
            bios_edit_commit();
            return;
        }
        if (kc == KEY_BACKSPACE) {
            if (s_bios.edit_len > 0) {
                s_bios.edit_len--;
                s_bios.edit_buf[s_bios.edit_len] = 0;
            }
            return;
        }
        // printable chars: ASCII 32..126, at most BIOS_EDIT_MAX
        if (ch >= 32 && ch <= 126 && s_bios.edit_len < BIOS_EDIT_MAX) {
            s_bios.edit_buf[s_bios.edit_len++] = ch;
            s_bios.edit_buf[s_bios.edit_len] = 0;
        }
        return;
    }

    // 3) page navigation
    bios_handle_page_key(w, e);
}

// ==================== Mouse support ====================

// classic BIOS is keyboard-first; optional mouse assist is provided here:
// click a menu row = select; clicking the selected row again = same as Enter (enter/execute).
static void bios_on_mouse(Window* w, int mx, int my, uint8_t buttons) {
    (void)mx;
    if (!(buttons & 0x01)) return;               // handle left-button presses only
    if (s_bios.confirm != CF_NONE || s_bios.edit != EDIT_NONE) return;

    // top tab strip: click to jump to a page
    if (my >= BIOS_TAB_Y && my < BIOS_TAB_Y + BIOS_TAB_H) {
        const BiosPage pages[BIOS_TAB_N] = { PAGE_MAIN, PAGE_BOOT, PAGE_SECURITY, PAGE_POWER, PAGE_EXIT };
        int total = BIOS_TAB_W * BIOS_TAB_N;
        int x0 = (w->back.width - total) / 2;
        int idx = (mx - x0) / BIOS_TAB_W;
        if (idx >= 0 && idx < BIOS_TAB_N) {
            s_bios.page = pages[idx];
        }
        return;
    }

    int n = 0;
    switch (s_bios.page) {
    case PAGE_MAIN:     n = 6; break;
    case PAGE_BOOT:     n = 8; break;
    case PAGE_BOOT_ADD: n = 5; break;
    case PAGE_BOOT_DEL: n = s_work.boot_entry_count > 0 ? s_work.boot_entry_count : 1; break;
    case PAGE_SECURITY: n = 5; break;
    case PAGE_POWER:    n = 6; break;
    case PAGE_EXIT:     n = 5; break;
    default: return;                             // read-only pages (SYSTEM) accept no clicks
    }
    if (my < BIOS_MENU_TOP) return;
    int row = (my - BIOS_MENU_TOP) / BIOS_ROW_H;
    if (row < 0 || row >= n) return;
    if (s_bios.sel[s_bios.page] != row) {
        s_bios.sel[s_bios.page] = row;           // single click: select only
    } else {
        KeyEvent ev;                             // click again: same as Enter
        ev.keycode = KEY_ENTER; ev.ascii = '\r'; ev.down = true;
        bios_handle_page_key(w, &ev);
    }
}

// ==================== Window callbacks and launch ====================

static void bios_on_paint(Window* w) {
    if (!w || !w->back.addr) return;
    bios_paint(w->back);
}

static void bios_on_close(Window* w) {
    (void)w;
    // window closed (ESC / top-right X): treat as "exit without saving"
    if (s_bios.running) {
        printf("[BIOS] Setup window closed, discarding changes.\n");
    }
    s_bios.running = false;
    s_bios_open = false;
}

// BIOS launch entry (called by the boot-time F2 check)
static void bios_launch() {
    if (s_bios_open) return;   // already running; avoid duplicate creation
    if (!g_wm) return;

    // initialize runtime state
    memset(&s_bios, 0, sizeof(s_bios));
    s_bios.page = PAGE_MAIN;
    s_bios.running = true;
    // load the working copy from the current system config
    s_work = g_uefi;
    if (s_work.username[0] == 0) strncpy(s_work.username, "user", sizeof(s_work.username) - 1);
    if (s_work.wallpaper_boot[0] == 0) strncpy(s_work.wallpaper_boot, "blue", sizeof(s_work.wallpaper_boot) - 1);
    if (s_work.wallpaper_lock[0] == 0) strncpy(s_work.wallpaper_lock, "dark", sizeof(s_work.wallpaper_lock) - 1);
    s_bios.boot_first = g_uefi.boot_first;
    if (s_work.boot_entry_count > NEFU_MAX_BOOT_ENTRIES) s_work.boot_entry_count = NEFU_MAX_BOOT_ENTRIES;
    if (s_work.boot_entry_count < 0) s_work.boot_entry_count = 0;
    s_bios.lock_on_suspend = g_settings.lock_on_suspend;
    s_bios.idle_lock_sec = g_settings.idle_lock_sec;
    s_bios.dirty = false;
    s_new_pwd[0] = 0;
    s_bios.boot_add_name[0] = 0;

    // create a full-screen BIOS window (fills the whole nefuOS screen: no title bar, covers desktop/taskbar)
    Window* bw = g_wm->create_window("nefuOS BIOS Setup", 0, 0, 800, 600, true);
    if (!bw) { s_bios.running = false; return; }
    bw->on_paint = bios_on_paint;
    bw->on_key = bios_on_key;
    bw->on_mouse = bios_on_mouse;
    bw->on_close = bios_on_close;
    bw->esc_close = false;      // ESC is handled by BIOS itself (back / confirm-exit)
    s_bios_open = true;
    g_wm->raise(bw);
    printf("[BIOS] nefuOS BIOS Setup Utility launched. ESC to exit, F10 to save & exit.\n");
}

}} // namespace nefu::apps
