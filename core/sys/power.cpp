#include "power.h"
#include "settings.h"
#include "../gui/lvgl_win.h"
#include "../gui/gfx.h"
#include "../klib/klib.h"
#include "../apps/apps.h"
#include "sha256.h"

namespace nefu {

bool g_locked = false;
UefiConfig g_uefi;
LvglWin* s_splash_win = nullptr;
LvglWin* s_lock_win = nullptr;
static lv_obj_t* s_splash_bar = nullptr;
static lv_obj_t* s_pwd_input = nullptr;

// Boot splash: fullscreen borderless LVGL window with logo + progress bar
void boot_splash_show(int progress) {
    if (!g_settings.boot_splash) return;
    if (!s_splash_win) {
        s_splash_win = lvgl_win_create("nefuOS Boot", 0, 0, 800, 600);
        lv_obj_set_style_bg_color(s_splash_win->content, lv_color_hex(0x1E293B), 0);
        // Centered logo
        lv_obj_t* logo = lv_label_create(s_splash_win->content);
        lv_label_set_text(logo, "nefuOS");
        lv_obj_set_style_text_color(logo, lv_color_hex(0x38BDF8), 0);
        lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);
        // Loading text
        lv_obj_t* sub = lv_label_create(s_splash_win->content);
        lv_label_set_text(sub, "Loading system components...");
        lv_obj_set_style_text_color(sub, lv_color_hex(0x94A3B8), 0);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, -20);
        // Progress bar control
        s_splash_bar = lv_bar_create(s_splash_win->content);
        lv_obj_set_size(s_splash_bar, 400, 12);
        lv_obj_align(s_splash_bar, LV_ALIGN_CENTER, 0, 60);
        lv_bar_set_range(s_splash_bar, 0, 100);
    }
    lv_bar_set_value(s_splash_bar, progress, LV_ANIM_OFF);
}

// Forward declarations
static void on_unlock_click(lv_event_t* e);
// Lock screen: fullscreen modal LVGL window with password input
void lock_screen() {
    g_locked = true;
    if (!s_lock_win) {
        s_lock_win = lvgl_win_create("nefuOS Lock", 0, 0, 800, 600);
        lv_obj_set_style_bg_color(s_lock_win->content, lv_color_hex(0x0F172A), 0);
        // Screen title
        lv_obj_t* title = lv_label_create(s_lock_win->content);
        lv_label_set_text(title, "nefuOS Lock Screen");
        lv_obj_set_style_text_color(title, lv_color_hex(0xF8FAFC), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -100);
        // Welcome username
        lv_obj_t* user = lv_label_create(s_lock_win->content);
        char user_txt[64];
        ksprintf(user_txt, sizeof(user_txt), "Welcome: %s", g_uefi.username);
        lv_label_set_text(user, user_txt);
        lv_obj_set_style_text_color(user, lv_color_hex(0x94A3B8), 0);
        lv_obj_align(user, LV_ALIGN_CENTER, 0, -50);
        // Password input field
        s_pwd_input = lv_textarea_create(s_lock_win->content);
        lv_textarea_set_password_mode(s_pwd_input, true);
        lv_obj_set_size(s_pwd_input, 280, 40);
        lv_obj_align(s_pwd_input, LV_ALIGN_CENTER, 0, 0);
        lv_textarea_set_placeholder_text(s_pwd_input, "Enter password...");
        // Unlock button
        lv_obj_t* btn = lv_button_create(s_lock_win->content);
        lv_obj_set_size(btn, 160, 36);
        lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "Unlock");
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, on_unlock_click, LV_EVENT_CLICKED, NULL);
    }
}

// Unlock button callback
static void on_unlock_click(lv_event_t* e) {
    if (!s_pwd_input) return;
    const char* pwd = lv_textarea_get_text(s_pwd_input);
    if (verify_password(pwd)) {
        unlock_screen();
    }
}

// Hash input and compare against stored UEFI password hash
bool verify_password(const char* input) {
    if (g_uefi.password_hash[0] == 0) return true; // No password set, allow unlock
    uint8_t hash[32];
    nefu_sha256((const uint8_t*)input, (uint32_t)strlen(input), hash);
    char hash_str[65];
    nefu_sha256_hex(hash, hash_str);
    return strcmp(hash_str, g_uefi.password_hash) == 0;
}

// Close LVGL lock window and return to desktop
void unlock_screen() {
    g_locked = false;
    if (s_lock_win) {
        lv_obj_del(s_lock_win->win);
        s_lock_win = nullptr;
        s_pwd_input = nullptr;
    }
}

// Save VFS snapshot to persistent storage
static void vfs_sync() {
    uint8_t* data = 0;
    uint32_t sz = 0;
    if (g_vfs && g_vfs->save(&data, &sz)) {
        platform_fs_save(data, sz);
        kfree(data);
    }
}

// Full shutdown: save config, sync VFS, power off
void os_shutdown() {
    settings_save();
    vfs_sync();
    platform_poweroff();
}

// Full reboot: save config, sync VFS, reset CPU
void os_reboot() {
    settings_save();
    vfs_sync();
    platform_reboot();
}

// Suspend: lock screen if required, enter low power state
void os_suspend() {
    if (g_settings.lock_on_suspend) {
        lock_screen();
    }
    platform_suspend();
}

// Parse semicolon-separated autostart list and launch each app
void autostart_run() {
    char apps[256];
    strncpy(apps, g_settings.autostart_apps, 255);
    apps[255] = 0;
    char* token = apps;
    while (token && *token) {
        char* semi = strchr(token, ';');
        if (semi) *semi = 0;
        if (token[0] != 0) app_launch(token);
        token = semi ? semi + 1 : 0;
    }
}

// Initialize power subsystem: load UEFI config, no splash until LVGL is ready
void power_init() {
    platform_uefi_load(&g_uefi);
    g_locked = false;
    s_splash_win = nullptr;
    s_lock_win = nullptr;
    s_splash_bar = nullptr;
    s_pwd_input = nullptr;
}

// ---- BSOD-style panic screen (fullscreen framebuffer overlay) --------------
// Painted last in nefuos_frame() INSTEAD of the desktop + windows, so it
// fills the ENTIRE nefuOS screen and no window can ever overlap it. It
// swallows all keyboard input and has no close button, so it cannot be
// dismissed: on entry it automatically runs system self-recovery and only
// "Reboot" restarts the OS ("Recovery" re-runs the repair).

bool g_crash_active = false;

struct BsodState {
    uint32_t code;
    char msg[48];
    CrashRegs regs;
    bool have_regs;
};
static BsodState s_bsod;

// Rebuild standard dirs + core files (same routine as the `recovery` command).
void system_recovery() {
    if (!g_vfs) return;
    g_vfs->ensure_standard_dirs();
    if (!g_vfs->resolve("/etc/passwd")) {
        g_vfs->mkdir("/etc");
        FSNode* f = g_vfs->create_file("/etc/passwd");
        if (f) {
            const char* c = "root:x:0:0:root:/root:/bin/sh\nnefu:x:1000:1000:nefu:/home/user:/bin/sh\n";
            g_vfs->write_file(f, (const uint8_t*)c, (uint32_t)strlen(c));
        }
    }
    if (!g_vfs->resolve("/var/lib/nefuos/db/system.db")) {
        g_vfs->mkdir("/var/lib/nefuos/db");
        FSNode* f = g_vfs->create_file("/var/lib/nefuos/db/system.db");
        if (f) {
            const char* c = "name=nefuOS\nversion=0.2.0\narch=x86_64\n";
            g_vfs->write_file(f, (const uint8_t*)c, (uint32_t)strlen(c));
        }
    }
    if (!g_vfs->resolve("/tmp")) g_vfs->mkdir("/tmp");
}

void blue_screen(uint32_t code, const char* msg, const CrashRegs* regs) {
    if (g_crash_active) return;   // already showing
    g_crash_active = true;
    s_bsod.code = code;
    s_bsod.msg[0] = 0;
    if (msg) { strncpy(s_bsod.msg, msg, 47); s_bsod.msg[47] = 0; }
    s_bsod.have_regs = (regs != 0);
    if (regs) s_bsod.regs = *regs;
    // Automatic self-recovery: rebuild the system files immediately so the
    // OS comes back to a usable state (idempotent, cannot fail hard).
    system_recovery();
}

// Paint the fullscreen blue screen (pure framebuffer drawing, no LVGL: safe
// to call from the crash-handler keep-alive loop as well as the frame loop).
void bsod_paint(Surface& fb) {
    const uint32_t NAVY = 0x000080;
    const uint32_t WHITE = 0xFFFFFF;
    const uint32_t LT = 0xC0C0C0;
    const uint32_t GREEN = 0x90EE90;
    fb.fill(NAVY);
    int x = 40, y = 16;
    gfx::text_ttf(fb, x, y, "nefuOS", WHITE, NAVY, 20); y += 34;
    gfx::text_ttf(fb, x, y, "nefuOS has encountered a problem", WHITE, NAVY, 16); y += 32;
    char line[128];
    ksprintf(line, sizeof(line), "STOP: 0x%08lX", (unsigned long)s_bsod.code);
    gfx::text_ttf(fb, x, y, line, WHITE, NAVY, 14); y += 26;
    if (s_bsod.msg[0]) {
        gfx::text_ttf(fb, x, y, s_bsod.msg, WHITE, NAVY, 14); y += 26;
    }
    gfx::text_ttf(fb, x, y, "Technical information:", WHITE, NAVY, 13); y += 24;
    if (s_bsod.have_regs) {
        ksprintf(line, sizeof(line), "  fault address: 0x%08lX",
                 (unsigned long)(s_bsod.regs.rip & 0xFFFFFFFFu));
        gfx::text_ttf(fb, x, y, line, WHITE, NAVY, 13); y += 24;
        ksprintf(line, sizeof(line), "  eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX",
                 (unsigned long)(s_bsod.regs.rax & 0xFFFFFFFFu),
                 (unsigned long)(s_bsod.regs.rbx & 0xFFFFFFFFu),
                 (unsigned long)(s_bsod.regs.rcx & 0xFFFFFFFFu),
                 (unsigned long)(s_bsod.regs.rdx & 0xFFFFFFFFu));
        gfx::text_ttf(fb, x, y, line, WHITE, NAVY, 13); y += 24;
        ksprintf(line, sizeof(line), "  esi=%08lX edi=%08lX ebp=%08lX esp=%08lX",
                 (unsigned long)(s_bsod.regs.rsi & 0xFFFFFFFFu),
                 (unsigned long)(s_bsod.regs.rdi & 0xFFFFFFFFu),
                 (unsigned long)(s_bsod.regs.rbp & 0xFFFFFFFFu),
                 (unsigned long)(s_bsod.regs.rsp & 0xFFFFFFFFu));
        gfx::text_ttf(fb, x, y, line, WHITE, NAVY, 13); y += 24;
        ksprintf(line, sizeof(line), "  eflags=%08lX  cs=0008  ss=0010",
                 (unsigned long)(s_bsod.regs.eflags & 0xFFFFFFFFu));
        gfx::text_ttf(fb, x, y, line, WHITE, NAVY, 13); y += 24;
    } else {
        ksprintf(line, sizeof(line), "  fault address: 0x%08lX", (unsigned long)s_bsod.code);
        gfx::text_ttf(fb, x, y, line, WHITE, NAVY, 13); y += 24;
        gfx::text_ttf(fb, x, y, "  eax=00000000 ebx=00000000 ecx=00000000 edx=00000000",
                      WHITE, NAVY, 13); y += 24;
        gfx::text_ttf(fb, x, y, "  esi=00000000 edi=00000000 ebp=00000000 esp=0001F000",
                      WHITE, NAVY, 13); y += 24;
        gfx::text_ttf(fb, x, y, "  eflags=00000246  cs=0008  ss=0010",
                      WHITE, NAVY, 13); y += 24;
    }
    y += 8;
    gfx::text_ttf(fb, x, y, "Automatic recovery: DONE - system files rebuilt", GREEN, NAVY, 13); y += 26;
    gfx::text_ttf(fb, x, y, "This screen cannot be dismissed. Click 'Reboot' to restart nefuOS.",
                  LT, NAVY, 13);
    // Buttons (bottom area)
    gfx::fillrect(fb, 40, 545, 150, 38, 0x004080);
    gfx::rect(fb, 40, 545, 150, 38, WHITE);
    gfx::text_ttf(fb, 78, 557, "Reboot", WHITE, 0x004080, 14);
    gfx::fillrect(fb, 210, 545, 190, 38, 0x004080);
    gfx::rect(fb, 210, 545, 190, 38, WHITE);
    gfx::text_ttf(fb, 248, 557, "Recovery", WHITE, 0x004080, 14);
}

// Blue-screen mouse hit-test (Reboot / Recovery). All other clicks and every
// key are swallowed, so the screen can never be dismissed.
void bsod_handle_mouse(int x, int y, uint8_t buttons) {
    static uint8_t s_prev = 0;
    bool pressed = (buttons & 1) != 0;
    bool was = (s_prev & 1) != 0;
    s_prev = buttons;
    if (!pressed || was) return;   // act on press edge only
    if (x >= 40 && x <= 190 && y >= 545 && y <= 583) { os_reboot(); return; }
    if (x >= 210 && x <= 400 && y >= 545 && y <= 583) { system_recovery(); return; }
}

}
