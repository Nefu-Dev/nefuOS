// nefuOS
#pragma once
#include "gfx.h"
#include "wm.h"
struct lv_group_t;  // LVGL group forward decl (apps include lvgl.h)

namespace nefu {

void desktop_init();
void desktop_paint(Surface& fb);
bool desktop_handle_mouse(int x, int y, uint8_t buttons);
bool desktop_handle_key(int keycode, char ascii);
void desktop_paint_boot(Surface& fb);
void desktop_paint_lock(Surface& fb);   // lock screen (UEFI password gate)

// lock-screen state owned by nefuos.cpp
bool nefuos_is_locked();
int nefuos_lock_len();
const char* nefuos_lock_pwd();
uint32_t nefuos_lock_fail_ms();
void nefuos_lock_screen();

// LVGL keyboard input: push a host key to LVGL, get the shared keypad group.
void lvgl_key_push(int keycode, char ascii);
lv_group_t* lvgl_kb_group();

} // namespace nefu
