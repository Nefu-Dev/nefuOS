// nefuOS system info - LVGL GUI
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../platform.h"

namespace nefu {

void sysinfo_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("System Info", x, y, 400, 260);
    if (!lw) return;
    lv_obj_t* body = lv_obj_create(lw->content);
    lv_obj_set_size(body, 400, 234);
    lv_obj_set_pos(body, 0, 0);
    lv_obj_set_style_bg_color(body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_pad_all(body, 8, 0);

    uint32_t used = 0, total = 0;
    platform_mem_stats(&used, &total);
    Screen* sc = platform_screen();

    struct Row { const char* t; uint32_t c; };
    char b0[96], b1[96], b2[96], b3[96], b4[96], b5[96];
    ksprintf(b0, sizeof(b0), "nefuOS v0.1.0");
    ksprintf(b1, sizeof(b1), "Backend    : %s", platform_name());
    ksprintf(b2, sizeof(b2), "Resolution : %dx%d (%d bpp)", sc->width, sc->height, 32);
    ksprintf(b3, sizeof(b3), "Uptime     : %u s", nefuos_uptime_ms() / 1000);
    ksprintf(b4, sizeof(b4), "Memory     : %u / %u KB", used / 1024, total / 1024);
    ksprintf(b5, sizeof(b5), "VFS        : %d nodes, %u bytes", g_vfs->node_count(), g_vfs->total_bytes());
    const char* rows[6] = { b0, b1, b2, b3, b4, b5 };
    uint32_t cols[6] = { 0x3366AA, 0x15181E, 0x15181E, 0x15181E, 0x15181E, 0x15181E };
    for (int i = 0; i < 6; i++) {
        lv_obj_t* lbl = lv_label_create(body);
        lv_label_set_text(lbl, rows[i]);
        lv_obj_set_pos(lbl, 4, i * 22);
        lv_obj_set_style_text_color(lbl, lv_color_hex(cols[i]), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    }
    lv_obj_t* foot1 = lv_label_create(body);
    lv_label_set_text(foot1, "Built with C++ / C, dual backend.  Host: nefuOS.exe  Bare: nefuOS.iso");
    lv_obj_set_pos(foot1, 4, 148);
    lv_obj_set_style_text_color(foot1, lv_color_hex(0x667788), 0);
    lv_obj_t* foot2 = lv_label_create(body);
    lv_label_set_text(foot2, "Try Terminal: ls / tree / cat");
    lv_obj_set_pos(foot2, 4, 172);
    lv_obj_set_style_text_color(foot2, lv_color_hex(0x5588CC), 0);
    lw->userdata = (void*)1;
}

} // namespace nefu