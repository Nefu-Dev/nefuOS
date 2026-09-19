// nefuOS notepad - LVGL GUI (lv_textarea, saves to /home/user/Documents/notes.txt)
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/desktop.h"
#include <cstring>

namespace nefu {

struct NoteLvState {
    LvglWin* lw;
    lv_obj_t* ta;
    lv_obj_t* status;
    String path;
    bool dirty;
};

static void note_lv_save(NoteLvState* st) {
    const char* txt = lv_textarea_get_text(st->ta);
    uint32_t len = (uint32_t)strlen(txt);
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (!f) {
        g_vfs->mkdir("/home/user/Documents");
        f = g_vfs->create_file(st->path.c_str());
    }
    if (f) {
        g_vfs->write_file(f, (const uint8_t*)txt, len);
        st->dirty = false;
        lv_label_set_text(st->status, "saved to /home/user/Documents/notes.txt");
    }
}

static void note_lv_save_btn(lv_event_t* e) {
    NoteLvState* st = (NoteLvState*)lv_event_get_user_data(e);
    if (st) note_lv_save(st);
}

static void note_lv_ta_cb(lv_event_t* e) {
    NoteLvState* st = (NoteLvState*)lv_event_get_user_data(e);
    if (st) {
        st->dirty = true;
        lv_label_set_text(st->status, "edited - click Save or press Ctrl+S");
    }
}

void notepad_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Notepad", x, y, 560, 380);
    if (!lw) return;
    NoteLvState* st = new NoteLvState();
    st->lw = lw;
    st->path = "/home/user/Documents/notes.txt";
    st->dirty = false;
    lw->userdata = st;

    // toolbar: Save button + status
    lv_obj_t* save_btn = lv_button_create(lw->content);
    lv_obj_set_pos(save_btn, 8, 6);
    lv_obj_set_size(save_btn, 64, 26);
    lv_obj_set_style_radius(save_btn, 4, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_add_event_cb(save_btn, note_lv_save_btn, LV_EVENT_CLICKED, st);
    lv_obj_t* sl = lv_label_create(save_btn);
    lv_label_set_text(sl, "Save");
    lv_obj_center(sl);
    lv_obj_set_style_text_color(sl, lv_color_hex(0xFFFFFF), 0);

    st->status = lv_label_create(lw->content);
    lv_obj_set_pos(st->status, 84, 10);
    lv_obj_set_style_text_color(st->status, lv_color_hex(0x667788), 0);
    lv_label_set_text(st->status, "click Save to store");

    // editor area
    st->ta = lv_textarea_create(lw->content);
    lv_obj_set_pos(st->ta, 8, 40);
    lv_obj_set_size(st->ta, 544, 310);
    lv_obj_set_style_bg_color(st->ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(st->ta, lv_color_hex(0x9AA5BF), 0);
    lv_textarea_set_placeholder_text(st->ta, "type here...");
    lv_textarea_set_one_line(st->ta, false);
    lv_textarea_set_cursor_click_pos(st->ta, true);
    lv_obj_add_event_cb(st->ta, note_lv_ta_cb, LV_EVENT_VALUE_CHANGED, st);

    // load existing file
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (f && !f->is_dir) {
        List<String> ls;
        file_to_lines(f, ls, 100000);
        String txt;
        int cap = ls.size();
        if (cap > 10000) cap = 10000;
        for (int i = 0; i < cap; i++) { txt += ls[i]; txt += "\n"; }
        lv_textarea_set_text(st->ta, txt.c_str());
    }

    // keyboard focus for the textarea
    lv_group_t* grp = lvgl_kb_group();
    if (grp) {
        lv_group_add_obj(grp, st->ta);
        lv_group_focus_obj(st->ta);
    }
}
} // namespace nefu