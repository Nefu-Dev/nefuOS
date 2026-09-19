// nefuOS text viewer - LVGL GUI
#include "apps.h"
#include "../gui/lvgl_win.h"

namespace nefu {

struct TVLvState {
    LvglWin* lw;
    lv_obj_t* body;    // scrollable container
    lv_obj_t* label;
    FSNode* file;
};

void app_show_textview(FSNode* file) {
    if (!file || file->is_dir) return;
    int x, y;
    cascade_pos(&x, &y);
    String title = file->name;
    title += " - Text Viewer";
    LvglWin* lw = lvgl_win_create(title.c_str(), x, y, 560, 360);
    if (!lw) return;
    TVLvState* st = new TVLvState();
    st->lw = lw;
    st->file = file;
    st->body = lv_obj_create(lw->content);
    lv_obj_set_size(st->body, 560, 334);
    lv_obj_set_pos(st->body, 0, 0);
    lv_obj_set_style_bg_color(st->body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(st->body, 0, 0);
    lv_obj_set_style_pad_all(st->body, 4, 0);
    lv_obj_set_scrollbar_mode(st->body, LV_SCROLLBAR_MODE_AUTO);

    st->label = lv_label_create(st->body);
    List<String> lines;
    file_to_lines(file, lines, 100000);
    String txt;
    int cap = lines.size();
    if (cap > 20000) cap = 20000;   // keep memory bounded
    for (int i = 0; i < cap; i++) {
        txt += lines[i];
        txt += "\n";
    }
    if (txt.empty()) txt = "(empty file)";
    lv_label_set_text(st->label, txt.c_str());
    lv_obj_set_style_text_color(st->label, lv_color_hex(0x15181E), 0);
    lv_obj_set_style_text_font(st->label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_line_space(st->label, 3, 0);
    int lh = cap * 17 + 12;
    if (lh < 320) lh = 320;
    lv_obj_set_size(st->label, 540, lh);
    lw->userdata = st;
}

} // namespace nefu