// ui.cpp —— UI 纯逻辑：按钮状态机、进度条比例、菜单命中、自测
#include "ui.h"

namespace nefu {
namespace gameengine {

// ============================================================================
//  UIButton 鼠标处理
//  约定：每帧调用一次，传入本帧鼠标位置与鼠标按键是否按下。
//  返回 true 表示本帧发生一次完整点击（按下->松开 在按钮内）。
// ============================================================================
bool UIButton::handle_mouse(int mx, int my, bool mouse_down) {
    clicked = false;
    if (state == UI_DISABLED) return false;

    bool inside = rect.contains(mx, my);

    if (!inside) {
        state = UI_NORMAL;
        prev_down = mouse_down;
        return false;
    }

    // 鼠标在按钮上
    if (mouse_down) {
        state = UI_PRESSED;
    } else {
        // 刚松开
        if (state == UI_PRESSED && prev_down) {
            clicked = true;
        }
        state = UI_HOVER;
    }
    prev_down = mouse_down;
    return clicked;
}

// ============================================================================
//  自测
// ============================================================================
int ui_self_test() {
    int fails = 0;

    // --- Rect 命中 ---
    UIRect r(10, 20, 100, 50);
    if (!r.contains(50, 40)) fails++;       // 内部
    if (r.contains(5, 40)) fails++;        // 左外
    if (r.contains(110, 40)) fails++;      // 右外（边界）
    if (r.contains(50, 75)) fails++;       // 下外

    // --- 按钮点击 ---
    UIButton btn;
    btn.rect = UIRect(100, 100, 80, 30);
    // 鼠标在按钮上，按下
    bool c1 = btn.handle_mouse(120, 110, true);
    if (c1) fails++;                  // 按下瞬间不触发 click
    if (btn.state != UI_PRESSED) fails++;
    // 松开 -> 触发 click
    bool c2 = btn.handle_mouse(120, 110, false);
    if (!c2) fails++;
    if (btn.state != UI_HOVER) fails++;
    // 移出按钮
    btn.handle_mouse(0, 0, false);
    if (btn.state != UI_NORMAL) fails++;

    // 按下后拖出松开：不应触发
    UIButton b2;
    b2.rect = UIRect(0, 0, 50, 50);
    b2.handle_mouse(10, 10, true);    // 按下
    bool c3 = b2.handle_mouse(200, 200, false);  // 移到外面松开
    if (c3) fails++;

    // --- 进度条 ---
    UIProgressBar bar;
    bar.rect = UIRect(0, 0, 100, 10);
    bar.min = 0; bar.max = 100; bar.value = 50;
    if (bar.fill_width() != 50) fails++;
    bar.set_value(25);
    if (bar.fill_width() != 25) fails++;
    bar.set_value(150);   // 超限钳制
    if (bar.value != 100) fails++;
    bar.set_value(-10);
    if (bar.value != 0) fails++;

    // 血条低血量变色
    UIHealthBar hb;
    hb.rect = UIRect(0, 0, 100, 10);
    hb.max = 100; hb.value = 90;
    if (hb.is_low()) fails++;
    if (hb.current_color() != hb.fill_color) fails++;
    hb.value = 20;
    if (!hb.is_low()) fails++;
    if (hb.current_color() != hb.low_color) fails++;

    // --- 菜单 ---
    UIMenu menu;
    menu.x = 10; menu.y = 10; menu.item_h = 24;
    menu.add_item("Start");
    menu.add_item("Options");
    menu.add_item("Quit");
    if (menu.count() != 3) fails++;
    if (menu.selected != 0) fails++;
    menu.move_down();
    if (menu.selected != 1) fails++;
    menu.move_down();
    menu.move_down();   // 已到底，不应越界
    if (menu.selected != 2) fails++;
    menu.move_up();
    if (menu.selected != 1) fails++;

    // 菜单命中
    int hit = menu.hit_test(20, 10);   // 第 0 项 y=10..34
    if (hit != 0) fails++;
    hit = menu.hit_test(20, 35);       // 第 1 项 y=34..58
    if (hit != 1) fails++;
    hit = menu.hit_test(20, 200);
    if (hit != -1) fails++;

    // --- 对话框 ---
    UIDialog dlg;
    dlg.open_dialog("Warning", 50, 50);
    if (!dlg.open) fails++;
    // 点 OK 按钮（rect x+20=70, y+80=130, 70x25 -> 中心 105,142）
    dlg.handle_mouse(105, 142, true);
    dlg.handle_mouse(105, 142, false);
    if (dlg.open) fails++;
    if (!dlg.result) fails++;

    // 点 Cancel
    UIDialog dlg2;
    dlg2.open_dialog("Quit?", 0, 0);
    // cancel rect: x+110=110, y+80=80, 70x25 -> 中心 145, 92
    dlg2.handle_mouse(145, 92, true);
    dlg2.handle_mouse(145, 92, false);
    if (dlg2.open) fails++;
    if (dlg2.result) fails++;


    // --- 滑块 ---
    UISlider sl;
    sl.rect = UIRect(10, 10, 100, 10);
    sl.min = 0; sl.max = 100; sl.value = 0;
    // 按下在中间，然后拖到最右边缘（rect 10..110，最右像素 109）
    sl.handle_mouse(60, 15, true);    // 按下 -> dragging
    if (sl.value < 48 || sl.value > 52) fails++;
    sl.handle_mouse(109, 15, true);   // 拖到右
    if (sl.value != 99) fails++;       // 99/100 -> 99
    sl.handle_mouse(15, 15, true);    // 拖到左
    if (sl.value != 5) fails++;
    // 松开后不再变
    sl.handle_mouse(15, 15, false);
    if (sl.value != 5) fails++;

    // --- 标签 ---
    UILabel lbl(5, 5, "HP", 0xFFFFFFFF);
    if (lbl.x != 5 || lbl.y != 5) fails++;

    // --- 文本输入 ---
    UITextField tf;
    tf.focus(true);
    tf.on_char('h'); tf.on_char('i');
    if (tf.length() != 2) fails++;
    if (tf.text[0] != 'h' || tf.text[1] != 'i') fails++;
    tf.on_backspace();
    if (tf.length() != 1) fails++;
    if (tf.text[0] != 'h') fails++;
    // 未聚焦不输入
    tf.focus(false);
    tf.on_char('x');
    if (tf.length() != 1) fails++;

    // --- FadeTransition ---
    FadeTransition ft;
    ft.fade_out(1000);
    ft.update(500);   // 半程
    if (ft.alpha < 100 || ft.alpha > 160) fails++;
    ft.update(600);   // 完成
    if (ft.active) fails++;
    if (ft.alpha != 255) fails++;

    // --- UIScrollPanel ---
    UIScrollPanel sp;
    sp.rect = UIRect(0, 0, 100, 50);
    sp.content_h = 200;
    sp.layout();
    if (sp.max_scroll != 150) fails++;
    sp.scroll(200);
    if (sp.scroll_y != 150) fails++;   // 夹到 max
    sp.scroll(-400);
    if (sp.scroll_y != 0) fails++;    // 夹到 0
    if (!sp.visible(0, 10)) fails++;   // 顶部可见

    // --- UINotification ---
    UINotification nt;
    nt.show("Hello", 1000);
    if (!nt.active) fails++;
    nt.update(500);
    if (nt.alpha() < 100 || nt.alpha() > 160) fails++;
    nt.update(600);
    if (nt.active) fails++;

    // --- UILayout ---
    UILayout lay(10, 100);
    UIRect r0 = lay.next(100, 20);
    if (r0.y != 100) fails++;
    UIRect r1 = lay.next(100, 20);
    if (r1.y != 124) fails++;   // 100 + 20 + 4


    return fails;
}

} // namespace gameengine
} // namespace nefu
