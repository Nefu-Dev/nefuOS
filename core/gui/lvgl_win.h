/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
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
    bool minimized;     // hidden by the title-bar "shrink" button, waiting in the taskbar
    bool system_window; // boot splash / lock screen / BSOD: never listed in the taskbar
    bool zoomed;        // title-bar maximize state (work-area sized)
    int  x, y, w, h;    // restored geometry for zoom / un-zoom
    char title[40];     // label shown by the taskbar for a minimized window
};

// Create a new LVGL app window. Returns 0 on allocation failure.
LvglWin* lvgl_win_create(const char* title, int x, int y, int w, int h);
// Replace the window title text.
void lvgl_win_set_title(LvglWin* w, const char* t);
// Delete the window and mark it closed (safe to call twice).
void lvgl_win_close(LvglWin* w);

// ---- shrink (minimize) support, shared with the desktop taskbar ----
// Every live LVGL window is registered, so "shrink" does not mean "gone":
// the window hides, the taskbar grows a button for it, a click on that button
// brings it back exactly where it was. The title-bar "maximize" button zooms
// the window to the work area and back.
bool  lvgl_win_minimize(LvglWin* w);
bool  lvgl_win_restore(LvglWin* w);
bool  lvgl_win_zoom_toggle(LvglWin* w);
// Registry access for the taskbar (newest last). Returns 0 for a bad index.
int   lvgl_win_count();
LvglWin* lvgl_win_at(int i);
// Changes whenever a window is opened, closed, shrunk or restored, so the
// taskbar can rebuild only when it actually became stale.
uint32_t lvgl_win_signature();

} // namespace nefu

