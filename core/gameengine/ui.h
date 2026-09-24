// ui.h —— 游戏 UI：按钮、进度条、血条、对话框、菜单、文本渲染、9 宫格面板
//
// 纯逻辑在 .cpp（布局/命中测试/状态机）；渲染 inline 在头文件。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "../gui/gfx.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

// 控件状态
enum UIState {
    UI_NORMAL = 0,
    UI_HOVER,
    UI_PRESSED,
    UI_DISABLED,
};

// ============================================================================
//  Rect —— 整数矩形
// ============================================================================
struct UIRect {
    int x, y, w, h;
    UIRect() : x(0), y(0), w(0), h(0) {}
    UIRect(int a, int b, int c, int d) : x(a), y(b), w(c), h(d) {}
    bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

// ============================================================================
//  UIButton —— 按钮
// ============================================================================
struct UIButton {
    UIRect      rect;
    const char* label;
    UIState     state;
    bool        clicked;     // 本帧被点击（边沿）
    bool        prev_down;   // 上一帧鼠标按下状态（边沿检测）
    uint32_t    color_normal, color_hover, color_pressed;

    UIButton() : label(0), state(UI_NORMAL), clicked(false), prev_down(false),
                 color_normal(0xFF444444), color_hover(0xFF666666),
                 color_pressed(0xFF888888) {}

    // 喂入鼠标位置/按键；返回是否本帧点击
    bool handle_mouse(int mx, int my, bool mouse_down);
};

// ============================================================================
//  UIProgressBar / UIHealthBar —— 进度条/血条
// ============================================================================
struct UIProgressBar {
    UIRect   rect;
    int      min, max, value;
    uint32_t bg_color, fill_color;

    UIProgressBar() : min(0), max(100), value(50),
                      bg_color(0xFF222222), fill_color(0xFF44AA44) {}

    float fraction() const {   // 用整数算比例，避免浮点
        if (max <= min) return 0;
        return (float)(value - min) / (float)(max - min);
    }
    // 整数比例（0..w）
    int fill_width() const {
        if (max <= min) return 0;
        return (int)((long long)(value - min) * rect.w / (max - min));
    }
    void set_value(int v) {
        if (v < min) v = min;
        if (v > max) v = max;
        value = v;
    }
};

struct UIHealthBar : UIProgressBar {
    uint32_t low_color;      // 低血量变红
    UIHealthBar() : low_color(0xFFCC3333) {}
    bool is_low() const { return fraction() < 0.3f; }
    uint32_t current_color() const { return is_low() ? low_color : fill_color; }
};

// ============================================================================
//  UIMenu —— 垂直菜单
// ============================================================================
struct UIMenu {
    List<const char*> items;
    int  selected;
    int  x, y, item_h;

    UIMenu() : selected(0), x(0), y(0), item_h(24) {}

    void add_item(const char* s) { items.push(s); }
    int count() const { return items.size(); }

    // 上下移动
    void move_up()   { if (selected > 0) selected--; }
    void move_down() { if (selected < items.size() - 1) selected++; }

    // 鼠标命中返回哪一项（-1=无）
    int hit_test(int mx, int my) const {
        for (int i = 0; i < items.size(); i++) {
            if (my >= y + i * item_h && my < y + (i + 1) * item_h &&
                mx >= x && mx < x + 120) return i;
        }
        return -1;
    }
};

// ============================================================================
//  UIDialog —— 模态对话框
// ============================================================================
struct UIDialog {
    UIRect       rect;
    const char*  title;
    UIButton     ok_button;
    UIButton     cancel_button;
    bool         open;
    bool         result;     // true=ok, false=cancel

    UIDialog() : title(0), open(false), result(false) {}

    void open_dialog(const char* t, int x, int y) {
        title = t;
        rect = UIRect(x, y, 200, 120);
        open = true;
        ok_button.rect = UIRect(x + 20, y + 80, 70, 25);
        ok_button.label = "OK";
        cancel_button.rect = UIRect(x + 110, y + 80, 70, 25);
        cancel_button.label = "Cancel";
        result = false;
    }

    void handle_mouse(int mx, int my, bool mouse_down) {
        if (!open) return;
        if (ok_button.handle_mouse(mx, my, mouse_down)) {
            result = true; open = false;
        }
        if (cancel_button.handle_mouse(mx, my, mouse_down)) {
            result = false; open = false;
        }
    }
};

// ============================================================================
//  NinePatch —— 9 宫格面板
// ============================================================================
struct NinePatch {
    int corner;     // 四角像素（不拉伸）
    uint32_t color;
    NinePatch() : corner(4), color(0xFF333333) {}
};

// ============================================================================
//  渲染 inline
// ============================================================================
inline void fill_rect(Surface& dst, int x, int y, int w, int h, uint32_t c) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || yy < 0 || xx >= dst.width || yy >= dst.height) continue;
            dst.setpx(xx, yy, c);
        }
}

inline void draw_button(Surface& dst, const UIButton& b) {
    uint32_t c;
    switch (b.state) {
        case UI_HOVER: c = b.color_hover; break;
        case UI_PRESSED: c = b.color_pressed; break;
        case UI_DISABLED: c = 0xFF222222; break;
        default: c = b.color_normal;
    }
    fill_rect(dst, b.rect.x, b.rect.y, b.rect.w, b.rect.h, c);
}

inline void draw_progressbar(Surface& dst, const UIProgressBar& p) {
    fill_rect(dst, p.rect.x, p.rect.y, p.rect.w, p.rect.h, p.bg_color);
    int fw = p.fill_width();
    if (fw > 0) fill_rect(dst, p.rect.x, p.rect.y, fw, p.rect.h, p.fill_color);
}

inline void draw_healthbar(Surface& dst, const UIHealthBar& p) {
    fill_rect(dst, p.rect.x, p.rect.y, p.rect.w, p.rect.h, p.bg_color);
    int fw = p.fill_width();
    if (fw > 0) fill_rect(dst, p.rect.x, p.rect.y, fw, p.rect.h, p.current_color());
}

inline void draw_ninepatch(Surface& dst, const UIRect& r, const NinePatch& np) {
    int c = np.corner;
    // 四角
    fill_rect(dst, r.x, r.y, c, c, np.color);
    fill_rect(dst, r.x + r.w - c, r.y, c, c, np.color);
    fill_rect(dst, r.x, r.y + r.h - c, c, c, np.color);
    fill_rect(dst, r.x + r.w - c, r.y + r.h - c, c, c, np.color);
    // 上下边
    fill_rect(dst, r.x + c, r.y, r.w - 2 * c, c, np.color);
    fill_rect(dst, r.x + c, r.y + r.h - c, r.w - 2 * c, c, np.color);
    // 左右边
    fill_rect(dst, r.x, r.y + c, c, r.h - 2 * c, np.color);
    fill_rect(dst, r.x + r.w - c, r.y + c, c, r.h - 2 * c, np.color);
    // 中心
    fill_rect(dst, r.x + c, r.y + c, r.w - 2 * c, r.h - 2 * c, np.color);
}

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  UISlider —— 水平滑块（0..100）
// ============================================================================
struct UISlider {
    UIRect rect;
    int    value;       // min..max
    int    min, max;
    bool   dragging;
    uint32_t bg_color, thumb_color;

    UISlider() : value(50), min(0), max(100), dragging(false),
                 bg_color(0xFF222222), thumb_color(0xFF38BDF8) {}

    int handle_mouse(int mx, int my, bool mouse_down) {
        if (mouse_down && rect.contains(mx, my)) dragging = true;
        if (!mouse_down) dragging = false;
        if (!dragging) return value;
        int rel = mx - rect.x;
        if (rel < 0) rel = 0;
        if (rel > rect.w) rel = rect.w;
        value = min + (max - min) * rel / rect.w;
        return value;
    }
    int thumb_x() const {
        int range = max - min;
        if (range <= 0) return rect.x;
        return rect.x + (value - min) * rect.w / range;
    }
};

struct UILabel {
    int x, y;
    const char* text;
    uint32_t color;
    UILabel() : x(0), y(0), text(0), color(0xFFFFFFFF) {}
    UILabel(int X, int Y, const char* t, uint32_t c) : x(X), y(Y), text(t), color(c) {}
};

// ============================================================================
//  UITextField —— 单行文本输入
// ============================================================================
const int GE_TEXT_MAX = 32;
struct UITextField {
    UIRect rect;
    char   text[GE_TEXT_MAX];
    int    cursor;
    bool   focused;

    UITextField() : cursor(0), focused(false) {
        for (int i = 0; i < GE_TEXT_MAX; i++) text[i] = 0;
    }

    void on_char(char c) {
        if (!focused) return;
        if (c >= 32 && c < 127 && cursor < GE_TEXT_MAX - 1) {
            text[cursor++] = c;
            text[cursor] = 0;
        }
    }
    void on_backspace() {
        if (!focused || cursor <= 0) return;
        cursor--;
        text[cursor] = 0;
    }
    void focus(bool f) { focused = f; }
    int length() const {
        int n = 0; while (text[n]) n++; return n;
    }
};

// ============================================================================
//  FadeTransition —— 屏幕淡入淡出过渡
// ============================================================================
struct FadeTransition {
    int  alpha;        // 0..255
    int  target;
    int  duration_ms;
    int  elapsed;
    bool active;
    uint32_t color;

    FadeTransition() : alpha(0), target(0), duration_ms(300),
                       elapsed(0), active(false), color(0xFF000000) {}

    void fade_in(int ms)  { alpha = 255; target = 0;   duration_ms = ms; elapsed = 0; active = true; }
    void fade_out(int ms) { alpha = 0;   target = 255; duration_ms = ms; elapsed = 0; active = true; }

    // 返回 true 表示仍在过渡
    bool update(int dt_ms) {
        if (!active) return false;
        elapsed += dt_ms;
        int t = (duration_ms > 0) ? elapsed * 255 / duration_ms : 255;
        if (t >= 255) { t = 255; active = false; }
        if (target > alpha) alpha = alpha + (255 - 0) * t / 255;
        else alpha = alpha - alpha * t / 255;
        if (alpha < 0) alpha = 0; if (alpha > 255) alpha = 255;
        return active;
    }
};

// ============================================================================
//  UIScrollPanel —— 可滚动面板
// ============================================================================
struct UIScrollPanel {
    UIRect rect;
    int    content_h;
    int    scroll_y;
    int    max_scroll;

    UIScrollPanel() : content_h(0), scroll_y(0), max_scroll(0) {}

    void layout() {
        max_scroll = content_h - rect.h;
        if (max_scroll < 0) max_scroll = 0;
    }
    void scroll(int dy) {
        scroll_y += dy;
        if (scroll_y < 0) scroll_y = 0;
        if (scroll_y > max_scroll) scroll_y = max_scroll;
    }
    // 世界坐标转面板内坐标
    int to_local_y(int wy) { return wy - rect.y + scroll_y; }
    bool visible(int item_y, int item_h) {
        int bottom = item_y + item_h - scroll_y;
        int top = item_y - scroll_y;
        return bottom >= rect.y && top <= rect.y + rect.h;
    }
};

// ============================================================================
//  UINotification —— 屏幕浮动通知/toast
// ============================================================================
struct UINotification {
    char text[64];
    int  life_ms;
    int  max_life;
    int  x, y;
    bool active;

    UINotification() : life_ms(0), max_life(1500), x(0), y(0), active(false) {
        for (int i = 0; i < 64; i++) text[i] = 0;
    }

    void show(const char* msg, int ms) {
        for (int i = 0; i < 63 && msg[i]; i++) text[i] = msg[i];
        text[63] = 0;
        life_ms = ms; max_life = ms; active = true;
    }
    bool update(int dt_ms) {
        if (!active) return false;
        life_ms -= dt_ms;
        if (life_ms <= 0) active = false;
        return active;
    }
    int alpha() const {
        if (!active) return 0;
        return life_ms * 255 / max_life;
    }
};

// ============================================================================
//  UILayout —— 简单垂直布局助手
// ============================================================================
struct UILayout {
    int x, y;
    int spacing;

    UILayout() : x(0), y(0), spacing(4) {}
    UILayout(int x_, int y_) : x(x_), y(y_), spacing(4) {}

    // 分配下一个矩形
    UIRect next(int w, int h) {
        UIRect r(x, y, w, h);
        y += h + spacing;
        return r;
    }
    void reset(int x_, int y_) { x = x_; y = y_; }
};

int ui_self_test();

} // namespace gameengine
} // namespace nefu
