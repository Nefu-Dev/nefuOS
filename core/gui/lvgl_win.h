// nefuOS LVGL application window container
// Replaces native wm windows for apps: each app gets a draggable LVGL
// window with a title bar and a close button, content lives in lv_win_get_content.
#pragma once
#include "lvgl.h"

namespace nefu {

struct LvglWin {
    lv_obj_t* win;      // lv_win root object
    lv_obj_t* content;  // content area (parent for app widgets)
    void* userdata;     // app state
    bool open;          // false after close
};

// Create a new LVGL app window. Returns 0 on allocation failure.
LvglWin* lvgl_win_create(const char* title, int x, int y, int w, int h);
// Replace the window title text.
void lvgl_win_set_title(LvglWin* w, const char* t);
// Delete the window and mark it closed (safe to call twice).
void lvgl_win_close(LvglWin* w);

} // namespace nefu
