// nefuOS
#pragma once
#include "gfx.h"
#include "wm.h"
struct lv_group_t;  // LVGL group forward decl (apps include lvgl.h)

namespace nefu {

void desktop_init();
void desktop_paint(Surface& fb);
bool desktop_handle_mouse(int x, int y, uint8_t buttons);
bool desktop_handle_key(int keycode, char ascii, uint8_t mods, const char* utf8);
// Scroll the LVGL object under the cursor (store list, start menu, ...).
// Returns true when the wheel was consumed by an LVGL object.
bool desktop_handle_scroll(int delta);
void desktop_paint_boot(Surface& fb);
void desktop_paint_lock(Surface& fb);   // lock screen (UEFI password gate)
void desktop_paint_setup(Surface& fb);  // first-boot setup wizard

// first-boot setup wizard state (owned by nefuos.cpp)
bool nefuos_setup_active();
int  nefuos_setup_field();              // 0=user 1=password 2=confirm 3=engine
int  nefuos_setup_engine();             // 0=minijs 1=noscript
const char* nefuos_setup_user();
int  nefuos_setup_pwd_len(int field);   // 0=user 1=password 2=confirm
uint32_t nefuos_setup_fail_ms();

// lock-screen state owned by nefuos.cpp
bool nefuos_is_locked();
int nefuos_lock_len();
const char* nefuos_lock_pwd();
uint32_t nefuos_lock_fail_ms();
void nefuos_lock_screen();

// LVGL keyboard input: push a host key to LVGL, get the shared keypad group.
void lvgl_key_push(int keycode, char ascii);
// Push a UTF-8 string (IME commit) into the focused LVGL textarea, char by char.
// Returns false when no LVGL textarea has keyboard focus (caller falls back
// to the classic WM, i.e. the editor / terminal on_key handler).
bool lvgl_key_push_utf8(const char* utf8);
lv_group_t* lvgl_kb_group();

// Start menu state: used by the window manager so open windows never draw
// over the menu (the menu floats above windows) and clicks reach it first.
bool start_menu_visible();
void start_menu_rect(int* x1, int* y1, int* x2, int* y2);

} // namespace nefu
