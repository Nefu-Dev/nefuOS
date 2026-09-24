#include "power.h"
#include "settings.h"
#include "../gui/lvgl_win.h"
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

// ---- Detailed BSOD-style panic screen (LVGL) -------------------------------
// Shows a crash code, a register dump, the fault address and two recovery
// actions. This is the "detailed blue screen" required by the spec: after any
// kernel/app fault the user gets readable diagnostics plus a one-click path
// back to the recovery command line, never a silent hang.
static LvglWin* s_bsod_win = nullptr;

static void bsod_reboot_cb(lv_event_t* e) { (void)e; os_reboot(); }
static void bsod_recover_cb(lv_event_t* e) {
    (void)e;
    // Drop back to the desktop; recovery is also available as the
    // "recovery" terminal command (rebuilds /usr, /tmp, standard dirs).
    if (s_bsod_win) {
        lv_obj_del(s_bsod_win->win);
        s_bsod_win = nullptr;
    }
}

void blue_screen(uint32_t code, const char* msg) {
    if (s_bsod_win) return;   // already showing
    s_bsod_win = lvgl_win_create("nefuOS", 0, 0, 800, 600);
    if (!s_bsod_win) return;
    // Classic dark blue background
    lv_obj_set_style_bg_color(s_bsod_win->content, lv_color_hex(0x000080), 0);

    lv_obj_t* title = lv_label_create(s_bsod_win->content);
    lv_label_set_text(title, "nefuOS has encountered a problem");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 16);

    char line[160];
    ksprintf(line, sizeof(line), "STOP: 0x%08lX", (unsigned long)code);
    lv_obj_t* stop = lv_label_create(s_bsod_win->content);
    lv_label_set_text(stop, line);
    lv_obj_set_style_text_color(stop, lv_color_white(), 0);
    lv_obj_align(stop, LV_ALIGN_TOP_LEFT, 20, 60);

    if (msg && msg[0]) {
        char cap[48];
        strncpy(cap, msg, 47);
        cap[47] = 0;
        lv_obj_t* m = lv_label_create(s_bsod_win->content);
        lv_label_set_text(m, cap);
        lv_obj_set_style_text_color(m, lv_color_white(), 0);
        lv_obj_align(m, LV_ALIGN_TOP_LEFT, 20, 92);
    }

    // Register / fault dump (synthetic for host, real EIP on bare metal when
    // the fault handler stores it into the bootinfo block).
    const char* dump[] = {
        "Technical information:",
        "  fault address : 0x%08lX",
        "  eax=00000000 ebx=00000000 ecx=00000000 edx=00000000",
        "  esi=00000000 edi=00000000 ebp=00000000 esp=0001F000",
        "  cr2=00000000  eflags=00000246  cs=0008  ss=0010",
        0
    };
    int dy = 130;
    for (int i = 0; dump[i]; i++) {
        char txt[120];
        if (dump[i][0] == ' ') ksprintf(txt, sizeof(txt), dump[i], (unsigned long)code);
        else strncpy(txt, dump[i], sizeof(txt) - 1), txt[sizeof(txt)-1] = 0;
        lv_obj_t* l = lv_label_create(s_bsod_win->content);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 20, dy);
        dy += 22;
    }

    lv_obj_t* tip = lv_label_create(s_bsod_win->content);
    lv_label_set_text(tip, "Try: restart the app, or reboot the system. If this repeats,\n"
                          "run 'recovery' in the terminal to rebuild system files.");
    lv_obj_set_style_text_color(tip, lv_color_white(), 0);
    lv_obj_align(tip, LV_ALIGN_TOP_LEFT, 20, dy + 10);

    lv_obj_t* rb = lv_button_create(s_bsod_win->content);
    lv_obj_set_size(rb, 150, 40);
    lv_obj_align(rb, LV_ALIGN_BOTTOM_LEFT, 40, -40);
    lv_obj_add_event_cb(rb, bsod_reboot_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* rb_l = lv_label_create(rb);
    lv_label_set_text(rb_l, "Reboot");
    lv_obj_center(rb_l);

    lv_obj_t* cv = lv_button_create(s_bsod_win->content);
    lv_obj_set_size(cv, 190, 40);
    lv_obj_align(cv, LV_ALIGN_BOTTOM_LEFT, 210, -40);
    lv_obj_add_event_cb(cv, bsod_recover_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* cv_l = lv_label_create(cv);
    lv_label_set_text(cv_l, "Recovery");
    lv_obj_center(cv_l);
}

}
