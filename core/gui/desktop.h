// nefuOS
#pragma once
#include "gfx.h"
#include "wm.h"

namespace nefu {

void desktop_init();
void desktop_paint(Surface& fb);
bool desktop_handle_mouse(int x, int y, uint8_t buttons);
bool desktop_handle_key(int keycode, char ascii);
void desktop_paint_boot(Surface& fb);

} // namespace nefu
