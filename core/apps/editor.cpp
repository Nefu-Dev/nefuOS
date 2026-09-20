// nefuOS code editor - LVGL GUI
// Opens text/code files from the VFS, edits in an lv_textarea and saves
// back to disk. Launched from the desktop icon (default file) or from the
// file manager (app_show_editor) for any text/code extension.
#include "apps.h"
#include "../gui/lvgl_win.h"
#include "../gui/desktop.h"
#include <cstring>

namespace nefu {

struct EditorLvState {
    LvglWin* lw;
    lv_obj_t* ta;
    lv_obj_t* status;
    String path;
    bool dirty;
};

// Reload current file content into the textarea.
static void editor_lv_reload(EditorLvState* st) {
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (!f || f->is_dir) {
        lv_textarea_set_text(st->ta, "");
        lv_label_set_text(st->status, "no file loaded - type and Save to create");
        return;
    }
    List<String> ls;
    file_to_lines(f, ls, 100000);
    String txt;
    int cap = ls.size();
    if (cap > 20000) cap = 20000;   // memory bound
    for (int i = 0; i < cap; i++) { txt += ls[i]; txt += "\n"; }
    lv_textarea_set_text(st->ta, txt.c_str());
    lv_label_set_text(st->status, (String("loaded ") + st->path).c_str());
    st->dirty = false;
}

static void editor_lv_save(EditorLvState* st) {
    const char* txt = lv_textarea_get_text(st->ta);
    uint32_t len = (uint32_t)strlen(txt);
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (!f) {
        // create parent dirs if needed
        int sl = st->path.rfind('/');
        if (sl > 0) {
            String dir = st->path.substr(0, sl);
            g_vfs->mkdir(dir.c_str());
        }
        f = g_vfs->create_file(st->path.c_str());
    }
    if (f) {
        g_vfs->write_file(f, (const uint8_t*)txt, len);
        st->dirty = false;
        lv_label_set_text(st->status, (String("saved ") + st->path).c_str());
    } else {
        lv_label_set_text(st->status, "save failed: cannot create file");
    }
}

static void editor_lv_save_btn(lv_event_t* e) {
    EditorLvState* st = (EditorLvState*)lv_event_get_user_data(e);
    if (st) editor_lv_save(st);
}

static void editor_lv_reload_btn(lv_event_t* e) {
    EditorLvState* st = (EditorLvState*)lv_event_get_user_data(e);
    if (st) editor_lv_reload(st);
}

static void editor_lv_ta_cb(lv_event_t* e) {
    EditorLvState* st = (EditorLvState*)lv_event_get_user_data(e);
    if (st && !st->dirty) {
        st->dirty = true;
        lv_label_set_text(st->status, "edited - click Save to write changes");
    }
}

static void editor_lv_new(EditorLvState* st) {
    lv_textarea_set_text(st->ta, "");
    st->dirty = false;
    lv_label_set_text(st->status, "new buffer - Save writes to current path");
}

static void editor_lv_new_btn(lv_event_t* e) {
    EditorLvState* st = (EditorLvState*)lv_event_get_user_data(e);
    if (st) editor_lv_new(st);
}

// shared window build for editor_launch / app_show_editor
static void editor_build(LvglWin* lw, EditorLvState* st) {
    // toolbar: New / Open / Save buttons
    lv_obj_t* new_btn = lv_button_create(lw->content);
    lv_obj_set_pos(new_btn, 8, 6);
    lv_obj_set_size(new_btn, 52, 26);
    lv_obj_set_style_radius(new_btn, 4, 0);
    lv_obj_set_style_bg_color(new_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_add_event_cb(new_btn, editor_lv_new_btn, LV_EVENT_CLICKED, st);
    lv_obj_t* nl = lv_label_create(new_btn);
    lv_label_set_text(nl, "New");
    lv_obj_center(nl);
    lv_obj_set_style_text_color(nl, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* open_btn = lv_button_create(lw->content);
    lv_obj_set_pos(open_btn, 66, 6);
    lv_obj_set_size(open_btn, 56, 26);
    lv_obj_set_style_radius(open_btn, 4, 0);
    lv_obj_set_style_bg_color(open_btn, lv_color_hex(0x3D4B66), 0);
    lv_obj_add_event_cb(open_btn, editor_lv_reload_btn, LV_EVENT_CLICKED, st);
    lv_obj_t* ol = lv_label_create(open_btn);
    lv_label_set_text(ol, "Open");
    lv_obj_center(ol);
    lv_obj_set_style_text_color(ol, lv_color_hex(0xFFFFFF), 0);

    lv_obj_t* save_btn = lv_button_create(lw->content);
    lv_obj_set_pos(save_btn, 128, 6);
    lv_obj_set_size(save_btn, 56, 26);
    lv_obj_set_style_radius(save_btn, 4, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x2E6E4E), 0);
    lv_obj_add_event_cb(save_btn, editor_lv_save_btn, LV_EVENT_CLICKED, st);
    lv_obj_t* sl = lv_label_create(save_btn);
    lv_label_set_text(sl, "Save");
    lv_obj_center(sl);
    lv_obj_set_style_text_color(sl, lv_color_hex(0xFFFFFF), 0);

    // status line
    st->status = lv_label_create(lw->content);
    lv_obj_set_pos(st->status, 196, 11);
    lv_obj_set_style_text_color(st->status, lv_color_hex(0x667788), 0);
    lv_obj_set_style_text_font(st->status, &lv_font_montserrat_14, 0);
    lv_label_set_text(st->status, "ready");

    // editor area
    st->ta = lv_textarea_create(lw->content);
    lv_obj_set_pos(st->ta, 8, 40);
    lv_obj_set_size(st->ta, 544, 300);
    lv_obj_set_style_bg_color(st->ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(st->ta, lv_color_hex(0x9AA5BF), 0);
    lv_obj_set_style_border_width(st->ta, 1, 0);
    lv_obj_set_style_text_font(st->ta, &lv_font_montserrat_14, 0);
    lv_textarea_set_placeholder_text(st->ta, "// code goes here...");
    lv_textarea_set_one_line(st->ta, false);
    lv_textarea_set_cursor_click_pos(st->ta, true);
    lv_textarea_set_accepted_chars(st->ta, NULL);
    lv_obj_add_event_cb(st->ta, editor_lv_ta_cb, LV_EVENT_VALUE_CHANGED, st);
}

void editor_launch() {
    int x, y;
    cascade_pos(&x, &y);
    LvglWin* lw = lvgl_win_create("Code Editor", x, y, 560, 380);
    if (!lw) return;
    EditorLvState* st = new EditorLvState();
    st->lw = lw;
    st->path = "/home/user/Documents/main.cpp";
    st->dirty = false;
    lw->userdata = st;
    editor_build(lw, st);

    // create a sample file when missing
    FSNode* f = g_vfs->resolve(st->path.c_str());
    if (!f) {
        g_vfs->mkdir("/home/user/Documents");
        f = g_vfs->create_file(st->path.c_str());
        if (f) {
            const char* sample =
                "// nefuOS sample program\n"
                "#include <nefu.h>\n"
                "\n"
                "int main() {\n"
                "    println(\"Hello from the Code Editor!\");\n"
                "    return 0;\n"
                "}\n";
            g_vfs->write_file(f, (const uint8_t*)sample, (uint32_t)strlen(sample));
        }
    }
    editor_lv_reload(st);

    lv_group_t* grp = lvgl_kb_group();
    if (grp) {
        lv_group_add_obj(grp, st->ta);
        lv_group_focus_obj(st->ta);
    }
}

void app_show_editor(FSNode* file) {
    if (!file || file->is_dir) return;
    int x, y;
    cascade_pos(&x, &y);
    String title = file->name;
    title += " - Code Editor";
    LvglWin* lw = lvgl_win_create(title.c_str(), x, y, 560, 380);
    if (!lw) return;
    EditorLvState* st = new EditorLvState();
    st->lw = lw;
    st->path = node_path(file);
    st->dirty = false;
    lw->userdata = st;
    editor_build(lw, st);
    editor_lv_reload(st);

    lv_group_t* grp = lvgl_kb_group();
    if (grp) {
        lv_group_add_obj(grp, st->ta);
        lv_group_focus_obj(st->ta);
    }
}

} // namespace nefu
