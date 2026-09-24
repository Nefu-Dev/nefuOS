// input.h —— 输入系统：键盘状态、鼠标、虚拟轴/按钮、输入映射、连击检测、触摸模拟
//
// 纯逻辑：由外层（窗口 on_key/on_mouse）把事件喂进来，引擎内部维护状态与抽象映射。
#pragma once

#include <stdint.h>
#include "../klib/klib.h"
#include "ge_math.h"

namespace nefu {
namespace gameengine {

const int GE_MAX_KEYS = 256;
const int GE_MAX_AXES = 16;
const int GE_MAX_ACTIONS = 32;

// ============================================================================
//  InputState —— 全局输入状态
// ============================================================================
struct InputState {
    // 键盘：down[code] 当前按下；pressed[code] 本帧刚按下；released[code] 本帧刚松开
    bool down[GE_MAX_KEYS];
    bool pressed[GE_MAX_KEYS];
    bool released[GE_MAX_KEYS];

    // 鼠标
    int  mouse_x, mouse_y;
    bool mouse_down[8];
    bool mouse_pressed[8];
    bool mouse_released[8];
    int  mouse_wheel;

    InputState() { clear(); }

    void clear() {
        for (int i = 0; i < GE_MAX_KEYS; i++) {
            down[i] = false; pressed[i] = false; released[i] = false;
        }
        mouse_x = mouse_y = 0; mouse_wheel = 0;
        for (int i = 0; i < 8; i++) {
            mouse_down[i] = false; mouse_pressed[i] = false; mouse_released[i] = false;
        }
    }

    // 喂入一个键盘事件
    void key_event(int code, bool is_down) {
        if (code < 0 || code >= GE_MAX_KEYS) return;
        if (is_down && !down[code]) pressed[code] = true;
        if (!is_down && down[code]) released[code] = true;
        down[code] = is_down;
    }

    // 喂入鼠标
    void mouse_event(int x, int y, uint8_t buttons) {
        mouse_x = x; mouse_y = y;
        for (int i = 0; i < 8; i++) {
            bool b = (buttons >> i) & 1;
            if (b && !mouse_down[i]) mouse_pressed[i] = true;
            if (!b && mouse_down[i]) mouse_released[i] = true;
            mouse_down[i] = b;
        }
    }
    void wheel_event(int delta) { mouse_wheel = delta; }

    // 每帧末调用：清除边沿（pressed/released）
    void end_frame() {
        for (int i = 0; i < GE_MAX_KEYS; i++) {
            pressed[i] = false;
            released[i] = false;
        }
        for (int i = 0; i < 8; i++) {
            mouse_pressed[i] = false;
            mouse_released[i] = false;
        }
        mouse_wheel = 0;
    }

    bool is_down(int code) const { return down[code]; }
    bool was_pressed(int code) const { return pressed[code]; }
    bool was_released(int code) const { return released[code]; }
};

// ============================================================================
//  虚拟轴/按钮：把物理键映射成抽象动作
// ============================================================================
struct VirtualAxis {
    int  neg_key;       // 负方向键（如左）
    int  pos_key;       // 正方向键（如右）
    fix  value;          // -1..1
    VirtualAxis() : neg_key(0), pos_key(0), value(0) {}
    void bind(int neg, int pos) { neg_key = neg; pos_key = pos; }
    void update(const InputState& in) {
        fix v = 0;
        if (in.is_down(pos_key)) v += fx::FX_ONE;
        if (in.is_down(neg_key)) v -= fx::FX_ONE;
        value = v;
    }
};

struct VirtualButton {
    int  key;
    bool down, pressed, released;
    VirtualButton() : key(0), down(false), pressed(false), released(false) {}
    void bind(int k) { key = k; }
    void update(const InputState& in) {
        down = in.is_down(key);
        pressed = in.was_pressed(key);
        released = in.was_released(key);
    }
};

// ============================================================================
//  ComboDetector —— 连击检测：在时间窗内按特定序列
// ============================================================================
struct ComboDetector {
    List<int> sequence;       // 需要的键序列
    int   window_ms;          // 时间窗
    int   progress;           // 当前匹配到第几步
    int   last_tick;          // 上一步时间
    bool  completed;

    ComboDetector() : window_ms(1000), progress(0), last_tick(0), completed(false) {}

    void set_sequence(const int* keys, int n, int window) {
        sequence.clear();
        for (int i = 0; i < n; i++) sequence.push(keys[i]);
        window_ms = window;
        progress = 0;
        completed = false;
    }

    // 喂入一个刚按下的键；返回是否完成整个连击
    bool on_key(int code, int now_ms) {
        if (progress > 0 && now_ms - last_tick > window_ms) progress = 0;  // 超时重置
        if (progress < sequence.size() && code == sequence[progress]) {
            progress++;
            last_tick = now_ms;
            if (progress >= sequence.size()) { completed = true; progress = 0; return true; }
        } else if (code == sequence[0]) {
            progress = 1;   // 从头开始
            last_tick = now_ms;
        } else {
            progress = 0;
        }
        return false;
    }
};

// ============================================================================
//  InputMapper —— 聚合：管理轴/按钮/连击
// ============================================================================
struct InputMapper {
    InputState state;
    List<VirtualAxis> axes;
    List<VirtualButton> buttons;

    void update() {
        for (int i = 0; i < axes.size(); i++) axes[i].update(state);
        for (int i = 0; i < buttons.size(); i++) buttons[i].update(state);
    }

    int add_axis(int neg, int pos) {
        VirtualAxis a; a.bind(neg, pos);
        axes.push(a);
        return axes.size() - 1;
    }
    int add_button(int key) {
        VirtualButton b; b.bind(key);
        buttons.push(b);
        return buttons.size() - 1;
    }
};

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  TapDetector / DragDetector —— 触摸/鼠标手势
// ============================================================================
struct TapDetector {
    int start_x, start_y;
    int start_tick;
    bool tracking;
    int max_dist;       // 超过此距离不算 tap
    int max_ms;        // 超过此时间不算 tap
    bool tapped;

    TapDetector() : start_x(0), start_y(0), start_tick(0), tracking(false),
                    max_dist(10), max_ms(300), tapped(false) {}

    void on_down(int x, int y, int now_ms) {
        start_x = x; start_y = y; start_tick = now_ms;
        tracking = true; tapped = false;
    }
    // 返回是否发生一次 tap
    bool on_up(int x, int y, int now_ms) {
        tapped = false;
        if (!tracking) return false;
        tracking = false;
        int dx = x - start_x, dy = y - start_y;
        int dist2 = dx*dx + dy*dy;
        int dt = now_ms - start_tick;
        if (dist2 < max_dist*max_dist && dt < max_ms) { tapped = true; return true; }
        return false;
    }
};

struct DragDetector {
    int start_x, start_y;
    int last_x, last_y;
    bool dragging;
    fix dx, dy;        // 本帧增量（像素，整数化）
    bool moved;

    DragDetector() : start_x(0), start_y(0), last_x(0), last_y(0),
                     dragging(false), dx(0), dy(0), moved(false) {}

    void on_down(int x, int y) {
        start_x = last_x = x; start_y = last_y = y;
        dragging = true; moved = false; dx = dy = 0;
    }
    void on_move(int x, int y) {
        if (!dragging) return;
        dx = x - last_x; dy = y - last_y;
        last_x = x; last_y = y;
        moved = true;
    }
    void on_up() { dragging = false; dx = dy = 0; }
};

// ============================================================================
//  SwipeDetector —— 滑动手势检测
// ============================================================================
struct SwipeDetector {
    int start_x, start_y;
    int cur_x, cur_y;
    bool tracking;
    int threshold;     // 像素
    int dir_x, dir_y;  // 0=未检测, 1=右, -1=左, 2=下, -2=上

    SwipeDetector() : start_x(0), start_y(0), cur_x(0), cur_y(0),
                      tracking(false), threshold(30), dir_x(0), dir_y(0) {}

    void begin(int x, int y) { start_x = x; start_y = y; cur_x = x; cur_y = y; tracking = true; dir_x = 0; dir_y = 0; }
    void move(int x, int y)  { cur_x = x; cur_y = y; }
    // 调用后返回是否完成一次滑动
    bool end() {
        if (!tracking) return false;
        tracking = false;
        int dx = cur_x - start_x, dy = cur_y - start_y;
        if (dx > threshold)       { dir_x = 1;  dir_y = 0; return true; }
        if (dx < -threshold)      { dir_x = -1; dir_y = 0; return true; }
        if (dy > threshold)       { dir_x = 0;  dir_y = 1;  return true; }
        if (dy < -threshold)      { dir_x = 0;  dir_y = -2; return true; }
        return false;
    }
};

// ============================================================================
//  VirtualStick —— 虚拟摇杆（模拟触摸）
// ============================================================================
struct VirtualStick {
    int center_x, center_y;
    int radius;
    int dx, dy;        // -radius..radius
    bool active;

    VirtualStick() : center_x(0), center_y(0), radius(40), dx(0), dy(0), active(false) {}

    void press(int x, int y) {
        active = true;
        move(x, y);
    }
    void move(int x, int y) {
        if (!active) return;
        dx = x - center_x;
        dy = y - center_y;
        // 限制在半径内
        int len = 0;
        // 简单近似：钳位
        if (dx > radius) dx = radius;
        if (dx < -radius) dx = -radius;
        if (dy > radius) dy = radius;
        if (dy < -radius) dy = -radius;
    }
    void release() { active = false; dx = 0; dy = 0; }
    fix axis_x() const { return (fix)dx * 65536 / radius; }
    fix axis_y() const { return (fix)dy * 65536 / radius; }
};
int input_self_test();

} // namespace gameengine
} // namespace nefu
