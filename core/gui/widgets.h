// nefuOS 小控件
#pragma once
#include "gfx.h"

namespace nefu {

struct Button {
    int x, y, w, h;
    const char* label;
    int id;
    bool pressed;
    void (*on_click)(void* ud);
    void* ud;
    Button() : x(0), y(0), w(0), h(0), label(0), id(0), pressed(false), on_click(0), ud(0) {}
};

namespace ui {

void draw_button(Surface& s, Button& b);
// 返回 true 表示事件被按钮消费
bool button_event(Button& b, int mx, int my, uint8_t buttons, bool pressed, bool released);

} // namespace ui
} // namespace nefu
