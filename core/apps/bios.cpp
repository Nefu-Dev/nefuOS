// nefuOS LVGL BIOS/UEFI Configuration Utility (uses existing project LVGL wrappers)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../sys/settings.h"
#include "../sys/power.h"
#include "../platform.h"
#include "../klib/klib.h"
#include "../sys/sha256.h"

namespace nefu {

static LvglWin* s_bios_win = nullptr;
static lv_obj_t* s_user_input = nullptr;
static lv_obj_t* s_new_pwd = nullptr;

// Save button callback
static void bios_save_click(lv_event_t* e) {
    (void)e;
    const char* user = lv_textarea_get_text(s_user_input);
    strncpy(g_uefi.username, user, 31);
    const char* newpwd = lv_textarea_get_text(s_new_pwd);
    if (strlen(newpwd) > 0) {
        uint8_t hash[32];
        nefu_sha256((const uint8_t*)newpwd, strlen(newpwd), hash);
        char hash_str[65];
        nefu_sha256_hex(hash, hash_str);
        strncpy(g_uefi.password_hash, hash_str, 63);
    }
    platform_uefi_save(&g_uefi);
    settings_save();
}

static lv_obj_t* bios_make_btn(LvglWin* win, const char* label, int x, int y, int w, int h, int id) {
    lv_obj_t* b = lv_button_create(win->content);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x3D4B66), 0);
    lv_obj_set_user_data(b, (void*)(intptr_t)id);
    lv_obj_t* lbl = lv_label_create(b);
    lv_label_set_text(lbl, label);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    return b;
}

void bios_launch() {
    if (s_bios_win) return;
    int x, y;
    cascade_pos(&x, &y);
    s_bios_win = lvgl_win_create("nefuOS BIOS/UEFI Setup", x, y, 480, 520);
    lv_obj_t* cont = s_bios_win->content;
    lv_obj_t* lbl;

    // Main settings section
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "=== BIOS/UEFI Configuration ===");
    lv_obj_set_pos(lbl, 20, 10);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "System Username:");
    lv_obj_set_pos(lbl, 20, 50);
    s_user_input = lv_textarea_create(cont);
    lv_textarea_set_text(s_user_input, g_uefi.username);
    lv_obj_set_size(s_user_input, 240, 36);
    lv_obj_set_pos(s_user_input, 150, 45);

    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "New Password:");
    lv_obj_set_pos(lbl, 20, 100);
    s_new_pwd = lv_textarea_create(cont);
    lv_textarea_set_password_mode(s_new_pwd, true);
    lv_obj_set_size(s_new_pwd, 240, 36);
    lv_obj_set_pos(s_new_pwd, 150, 95);

    // Hardware info section
    lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "--- Hardware Info ---");
    lv_obj_set_pos(lbl, 20, 155);
    HwInfo hw;
    platform_hw_info(&hw);
    char line[128];
    ksprintf(line, sizeof(line), "CPU: %s @ %dMHz", hw.cpu_model, hw.cpu_mhz);
    lbl = lv_label_create(cont); lv_label_set_text(lbl, line); lv_obj_set_pos(lbl, 20, 180);
    ksprintf(line, sizeof(line), "Total Memory: %d MB", (int)hw.mem_total_mb);
    lbl = lv_label_create(cont); lv_label_set_text(lbl, line); lv_obj_set_pos(lbl, 20, 200);
    ksprintf(line, sizeof(line), "BIOS: %s %s", hw.bios_vendor, hw.bios_version);
    lbl = lv_label_create(cont); lv_label_set_text(lbl, line); lv_obj_set_pos(lbl, 20, 220);

    DiskInfo disks[8];
    int nd = platform_disk_scan(disks, 8);
    for (int i=0; i<nd; i++) {
        ksprintf(line, sizeof(line), "Disk %s: %s %dMB", disks[i].name, disks[i].model, (int)(disks[i].sectors/2048));
        lbl = lv_label_create(cont); lv_label_set_text(lbl, line); lv_obj_set_pos(lbl, 20, 240 + i*20);
    }

    // Buttons
    bios_make_btn(s_bios_win, "Save & Exit", 60, 460, 140, 36, 0);
    bios_make_btn(s_bios_win, "Cancel", 260, 460, 140, 36, 1);
    // Attach save callback
    lv_obj_t* save_btn = lv_obj_get_child(cont, 12);
    lv_obj_add_event_cb(save_btn, bios_save_click, LV_EVENT_CLICKED, NULL);
}

}
