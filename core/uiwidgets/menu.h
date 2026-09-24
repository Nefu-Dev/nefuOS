// nefuOS UI 组件库 —— 菜单组件
// 菜单栏(水平菜单)、上下文菜单(右键弹出)、菜单项(文字/快捷键/分隔线/子菜单)、快捷键绑定
#pragma once

#include "widget.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

class MenuItem {
public:
    MenuItem();
    explicit MenuItem(const char* text);
    ~MenuItem();

    String text;
    int    shortcut;       // 快捷键 ascii(0=无)
    bool   separator;      // 是否为分隔线
    bool   enabled;
    UxCallback on_click;
    void* user;

    List<MenuItem*> children;   // 子菜单
    MenuItem* parent;

    void add(MenuItem* item);
    MenuItem* add_item(const char* text, int shortcut = 0);
    void add_separator();
    int  depth() const;         // 嵌套深度(自检用)
};

// 菜单栏(水平)
class MenuBar : public Widget {
public:
    explicit MenuBar(Widget* parent = 0);
    MenuItem* add_menu(const char* text);   // 顶层菜单
    int  item_count() const { return items_.size(); }
    MenuItem* item(int i) const { return items_[i]; }
    int  find_shortcut(int key) const;       // 全局快捷键查找,返回菜单项索引或 -1
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    List<MenuItem*> items_;
    int open_index_;
};

// 弹出菜单(垂直,用于右键上下文)
class PopupMenu : public Widget {
public:
    explicit PopupMenu(Widget* parent = 0);
    ~PopupMenu();
    MenuItem* root;                 // 持有所有菜单项
    void add_item(const char* text, int shortcut = 0);
    void add_separator();
    int  visible_count() const;     // 含分隔线的总行数
    void open_at(int x, int y);
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    int row_h() const { return 16; }
};

// ===================== 模块自检 =====================
int menu_self_test();

} // namespace ui
} // namespace nefu
