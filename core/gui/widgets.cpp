// nefuOS 小控件实现
#include "widgets.h"

namespace nefu {
namespace ui {

void draw_button(Surface& s, Button& b) {
    uint32_t bg = b.pressed ? 0x00C4C3BD : 0x00E6E5E0;
    gfx::fillrect(s, b.x, b.y, b.w, b.h, bg);
    gfx::rect(s, b.x, b.y, b.w, b.h, b.pressed ? color::BORDER : 0x00B0AFA8);
    // 标签居中
    int tw = gfx::text_width(b.label);
    int tx = b.x + (b.w - tw) / 2;
    int ty = b.y + (b.h - 16) / 2;
    gfx::text(s, tx, ty, b.label, color::TEXT, bg);
}

bool button_event(Button& b, int mx, int my, uint8_t buttons, bool pressed, bool released) {
    (void)buttons;
    bool inside = (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h);
    if (pressed && inside) b.pressed = true;
    if (released && b.pressed) {
        b.pressed = false;
        if (inside && b.on_click) b.on_click(b.ud);
        return true;
    }
    return inside;
}

} // namespace ui
} // namespace nefu
