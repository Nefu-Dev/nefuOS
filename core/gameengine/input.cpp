// input.cpp —— 输入系统自测
#include "input.h"

namespace nefu {
namespace gameengine {

int input_self_test() {
    int fails = 0;

    // --- 键盘边沿 ---
    InputState in;
    in.key_event(65, true);    // 按下 A
    if (!in.is_down(65)) fails++;
    if (!in.was_pressed(65)) fails++;
    in.end_frame();
    // end_frame 后 pressed 清除，但 down 保留
    if (!in.is_down(65)) fails++;
    if (in.was_pressed(65)) fails++;

    in.key_event(65, false);   // 松开
    if (!in.was_released(65)) fails++;
    if (in.is_down(65)) fails++;
    in.end_frame();

    // 重复按下不算新的 pressed
    in.key_event(66, true);
    if (!in.was_pressed(66)) fails++;
    in.key_event(66, true);    // 已经按着，再发一次 down：不改变 down/pressed 状态
    if (!in.is_down(66)) fails++;

    // --- 虚拟轴 ---
    InputMapper mapper;
    // 键 37=左, 39=右
    int ax = mapper.add_axis(37, 39);
    mapper.state.key_event(39, true);   // 按右
    mapper.update();
    if (mapper.axes[ax].value != fx::FX_ONE) fails++;
    mapper.state.key_event(37, true);   // 同时按左
    mapper.update();
    if (mapper.axes[ax].value != 0) fails++;

    // --- 虚拟按钮 ---
    int btn = mapper.add_button(32);   // 空格
    mapper.state.key_event(32, true);
    mapper.update();
    if (!mapper.buttons[btn].pressed) fails++;
    if (!mapper.buttons[btn].down) fails++;
    mapper.state.end_frame();
    mapper.update();
    if (mapper.buttons[btn].pressed) fails++;   // 下一帧不再 pressed

    // --- 鼠标 ---
    InputState mi;
    mi.mouse_event(100, 200, 0x01);   // 左键按下
    if (mi.mouse_x != 100 || mi.mouse_y != 200) fails++;
    if (!mi.mouse_down[0]) fails++;
    if (!mi.mouse_pressed[0]) fails++;
    mi.end_frame();
    mi.mouse_event(100, 200, 0x00);   // 松开
    if (!mi.mouse_released[0]) fails++;

    // --- 连击检测 ---
    ComboDetector combo;
    int seq[3] = { 88, 89, 90 };   // X Y Z
    combo.set_sequence(seq, 3, 500);
    // X
    if (combo.on_key(88, 0)) fails++;
    // Y（100ms 内）
    if (combo.on_key(89, 100)) fails++;
    // Z -> 完成
    if (!combo.on_key(90, 200)) fails++;
    if (!combo.completed) fails++;

    // 超时重置：先按 X，等 600ms 再按 YZ
    ComboDetector combo2;
    combo2.set_sequence(seq, 3, 500);
    combo2.on_key(88, 0);
    if (combo2.on_key(89, 600)) fails++;   // 超时，不算第二步
    if (combo2.on_key(90, 650)) fails++;   // 单独的 Z 不触发
    if (combo2.completed) fails++;

    // 错误键重置
    ComboDetector combo3;
    combo3.set_sequence(seq, 3, 500);
    combo3.on_key(88, 0);
    combo3.on_key(65, 50);    // 错误键
    if (combo3.on_key(89, 100)) fails++;   // 序列被打断
    if (combo3.on_key(90, 150)) fails++;


    // --- Tap 手势 ---
    TapDetector tap;
    tap.on_down(100, 100, 0);
    if (tap.on_up(102, 99, 150)) { /* 小位移、短时间 -> tap */ }
    else fails++;
    // 长位移不算 tap
    TapDetector tap2;
    tap2.on_down(0, 0, 0);
    if (tap2.on_up(200, 200, 50)) fails++;
    // 长时间不算 tap
    TapDetector tap3;
    tap3.on_down(0, 0, 0);
    if (tap3.on_up(2, 2, 500)) fails++;

    // --- Drag 手势 ---
    DragDetector drag;
    drag.on_down(50, 50);
    drag.on_move(60, 80);
    if (drag.dx != 10 || drag.dy != 30) fails++;
    if (!drag.moved) fails++;
    drag.on_up();
    if (drag.dragging) fails++;

    // --- SwipeDetector ---
    SwipeDetector sw;
    sw.begin(0, 0);
    sw.move(100, 5);
    if (!sw.end()) fails++;
    if (sw.dir_x != 1) fails++;   // 右滑
    sw.begin(0, 0);
    sw.move(5, 100);
    if (!sw.end()) fails++;
    if (sw.dir_y != 1) fails++;    // 下滑

    // --- VirtualStick ---
    VirtualStick vs;
    vs.center_x = 100; vs.center_y = 100; vs.radius = 40;
    vs.press(100, 100);
    if (vs.axis_x() != 0) fails++;
    vs.move(140, 100);   // 右满
    if (vs.dx != 40) fails++;
    vs.move(200, 100);   // 超出，钳位
    if (vs.dx != 40) fails++;
    vs.release();
    if (vs.active) fails++;
    return fails;
}

} // namespace gameengine
} // namespace nefu
