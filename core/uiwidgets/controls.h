// nefuOS UI 组件库 —— 标准控件集
// Button / Label / TextField / TextArea / CheckBox / RadioButton / Switch /
// Slider / ProgressBar / Spinner / ComboBox / ListBox / TreeView
#pragma once

#include "widget.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

// ===================== Button 按钮 =====================
class Button : public Widget {
public:
    explicit Button(Widget* parent = 0);
    void set_text(const char* s);
    const char* text() const { return label_.c_str(); }
    void set_on_click(UxCallback cb, void* user = 0) { on_click_ = cb; click_user_ = user; }
    // 程序化触发(自检用)
    void click();

    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    String label_;
    UxCallback on_click_;
    void* click_user_;
};

// ===================== Label 标签 =====================
enum Align { AlignLeft = 0, AlignCenter, AlignRight };

class Label : public Widget {
public:
    explicit Label(Widget* parent = 0);
    void set_text(const char* s);
    const char* text() const { return text_.c_str(); }
    void set_align(Align a) { align_ = a; }
    void set_color(gfxlib::Pixel c) { color_ = c; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    String text_;
    Align  align_;
    gfxlib::Pixel color_;
};

// ===================== TextField 单行输入 =====================
class TextField : public Widget {
public:
    explicit TextField(Widget* parent = 0);
    void set_text(const char* s);
    const char* text() const { return buf_.c_str(); }
    void set_placeholder(const char* s) { ph_ = s; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
    virtual void on_key(const UxKeyEvent& e) override;
private:
    String buf_;
    String ph_;
    int    cursor_;       // 光标位置(字符索引)
    int    scroll_x_;     // 水平滚动(像素)
    void move_cursor(int d);
};

// ===================== TextArea 多行文本 =====================
class TextArea : public Widget {
public:
    explicit TextArea(Widget* parent = 0);
    void set_text(const char* s);
    const char* text() const { return buf_.c_str(); }
    int  line_count() const { return lines_.size(); }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
    virtual void on_key(const UxKeyEvent& e) override;
private:
    String buf_;
    List<String> lines_;
    int    scroll_line_;
    void   rebuild_lines();
};

// ===================== CheckBox 复选框 =====================
class CheckBox : public Widget {
public:
    explicit CheckBox(Widget* parent = 0);
    void set_text(const char* s);
    void set_checked(bool c);
    bool is_checked() const { return checked_; }
    void set_on_toggle(UxCallback cb, void* user = 0) { on_toggle_ = cb; toggle_user_ = user; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
protected:
    String label_;
    bool   checked_;
    UxCallback on_toggle_;
    void* toggle_user_;
};

// ===================== RadioButton 单选(同组互斥) =====================
struct RadioGroup {
    Widget* checked = 0;   // 当前选中的 RadioButton
};

class RadioButton : public CheckBox {
public:
    explicit RadioButton(Widget* parent = 0);
    void set_group(RadioGroup* g) { group_ = g; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    RadioGroup* group_;
};

// ===================== Switch 开关 =====================
class Switch : public Widget {
public:
    explicit Switch(Widget* parent = 0);
    void set_on(bool v);
    bool is_on() const { return on_; }
    void set_on_toggle(UxCallback cb, void* user = 0) { on_toggle_ = cb; toggle_user_ = user; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    bool on_;
    UxCallback on_toggle_;
    void* toggle_user_;
};

// ===================== Slider 滑块 =====================
class Slider : public Widget {
public:
    explicit Slider(Widget* parent = 0);
    void set_range(int lo, int hi);
    void set_value(int v);
    int  value() const { return value_; }
    bool horizontal() const { return horiz_; }
    void set_horizontal(bool h) { horiz_ = h; }
    void set_on_change(UxCallback cb, void* user = 0) { on_change_ = cb; change_user_ = user; }
    // 值->手柄位置(本地坐标,自检用)
    int  knob_position() const;
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    int lo_, hi_, value_;
    bool horiz_;
    UxCallback on_change_;
    void* change_user_;
};

// ===================== ProgressBar 进度条 =====================
class ProgressBar : public Widget {
public:
    explicit ProgressBar(Widget* parent = 0);
    void set_range(int lo, int hi);
    void set_value(int v);
    int  value() const { return value_; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    int lo_, hi_, value_;
};

// ===================== Spinner 数值增减 =====================
class Spinner : public Widget {
public:
    explicit Spinner(Widget* parent = 0);
    void set_range(int lo, int hi);
    void set_value(int v);
    int  value() const { return value_; }
    void step_up();
    void step_down();
    void set_on_change(UxCallback cb, void* user = 0) { on_change_ = cb; change_user_ = user; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    int lo_, hi_, value_;
    UxCallback on_change_;
    void* change_user_;
};

// ===================== ScrollBar 滚动条 =====================
class ScrollBar : public Widget {
public:
    explicit ScrollBar(Widget* parent = 0);
    void set_range(int visible, int total);   // 可视行数 / 总行数
    void set_value(int v);
    int  value() const { return value_; }
    bool horizontal() const { return horiz_; }
    void set_horizontal(bool h) { horiz_ = h; }
    int  knob_size() const;                   // 滑块长度(自检用)
    int  knob_pos() const;                    // 滑块起始位置
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    int visible_, total_, value_;
    bool horiz_;
};

// ===================== Separator 分隔条 =====================
class Separator : public Widget {
public:
    explicit Separator(Widget* parent = 0);
    bool horizontal;
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
};

// ===================== StatusBar 状态栏 =====================
class StatusBar : public Widget {
public:
    explicit StatusBar(Widget* parent = 0);
    void set_text(const char* s, int idx = 0);
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    String fields_[3];
};

// ===================== ComboBox 下拉框 =====================
class ComboBox : public Widget {
public:
    explicit ComboBox(Widget* parent = 0);
    void add_item(const char* s);
    int  item_count() const { return items_.size(); }
    const char* item(int i) const { return items_[i].c_str(); }
    void set_selected(int i);
    int  selected() const { return sel_; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    List<String> items_;
    int sel_;
    bool open_;
};

// ===================== ListBox 列表框 =====================
class ListBox : public Widget {
public:
    explicit ListBox(Widget* parent = 0);
    void clear();
    void add_item(const char* s);
    int  item_count() const { return items_.size(); }
    const char* item(int i) const { return items_[i].c_str(); }
    void set_selected(int i);
    int  selected() const { return sel_; }
    int  visible_rows() const;
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
    virtual void on_key(const UxKeyEvent& e) override;
private:
    List<String> items_;
    int sel_;
    int scroll_row_;
    int row_h() const { return 16; }
};

// ===================== TreeView 树 =====================
struct TreeNode {
    String text;
    bool expanded = false;
    List<TreeNode*> children;
    ~TreeNode() { for (int i = 0; i < children.size(); i++) delete children[i]; }
};

class TreeView : public Widget {
public:
    explicit TreeView(Widget* parent = 0);
    ~TreeView();
    TreeNode* root;
    TreeNode* add_root(const char* text);
    TreeNode* add_child(TreeNode* parent, const char* text);
    int  total_visible() const;          // 展开状态下可见节点数(自检用)
    void toggle(TreeNode* n) { if (n) n->expanded = !n->expanded; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
private:
    // 前序遍历收集可见节点
    int collect_visible(TreeNode* n, TreeNode** out, int max) const;
};

// ===================== GroupBox 分组框(带标题的容器) =====================
class GroupBox : public Widget {
public:
    explicit GroupBox(Widget* parent = 0);
    void set_title(const char* s) { title_ = s; }
    const char* title() const { return title_.c_str(); }
    int  content_top() const { return 18; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    String title_;
};

// ===================== 模块自检 =====================
int controls_self_test();

} // namespace ui
} // namespace nefu
