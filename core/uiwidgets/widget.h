// nefuOS UI 组件库 —— Widget 基类
// 纯软件绘制:所有组件渲染到 gfxlib::Buffer(离屏像素缓冲),不依赖 LVGL / 不直接操作硬件。
// 设计目标:可在裸机与宿主(Win32)两端编译,仅依赖 gfxlib + klib。
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "gfxlib/gfxlib_all.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

// ===================== 主题配色(扁平化浅色主题) =====================
struct UxTheme {
    gfxlib::Pixel bg;          // 窗口背景
    gfxlib::Pixel panel;       // 面板/卡片背景
    gfxlib::Pixel border;      // 边框
    gfxlib::Pixel text;        // 主文字
    gfxlib::Pixel text_dim;    // 次要文字
    gfxlib::Pixel accent;      // 强调色(主按钮/选中)
    gfxlib::Pixel accent_dim;  // 按下态
    gfxlib::Pixel track;       // 滑轨/进度背景
    gfxlib::Pixel selection;   // 文本选中高亮
    gfxlib::Pixel white;
};

// 全局主题(单一实例)
const UxTheme& theme();

// ===================== 事件结构 =====================
// 鼠标事件坐标为"相对本控件左上角"的本地坐标
struct UxMouseEvent {
    int  x;        // 本地 x
    int  y;        // 本地 y
    int  button;   // 0=左键 1=右键 2=中键
    bool down;     // true=按下 false=释放
};

// 键盘事件(ascii 为可打印字符,keycode 为平台键码;这里只做语义层 keycode)
struct UxKeyEvent {
    int  keycode;  // 语义键码(见下方 KEY_* 常量)
    int  ascii;    // 可打印 ASCII
    bool down;
};

// 语义键码(与 wm.h 的 KeyEvent 对齐的子集,库内自洽)
enum {
    UX_KEY_BACKSPACE = 8,
    UX_KEY_TAB       = 9,
    UX_KEY_ENTER     = 13,
    UX_KEY_ESC       = 27,
    UX_KEY_LEFT      = 1000,
    UX_KEY_RIGHT,
    UX_KEY_UP,
    UX_KEY_DOWN,
    UX_KEY_HOME,
    UX_KEY_END,
    UX_KEY_DELETE,
    UX_KEY_SPACE,
    UX_KEY_PRIOR,    // PageUp
    UX_KEY_NEXT      // PageDown
};

// 通用控件回调:target 为发出事件的对象(可强转回具体控件类型),user 为注册时附带的上下文
typedef void (*UxCallback)(void* target, void* user);

// ===================== 布局方式 =====================
enum LayoutKind {
    LayNone = 0,   // 绝对定位(子控件使用自身 x/y/w/h)
    LayVBox,       // 垂直盒子:子控件自上而下排列
    LayHBox,       // 水平盒子:子控件自左而右排列
    LayGrid        // 网格:按 grid_cols 列换行排列
};

// ===================== 矩形与脏区域 =====================
struct UxRect {
    int x, y, w, h;
    bool empty() const { return w <= 0 || h <= 0; }
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    void unite(const UxRect& o);          // 与另一矩形求并集
    bool intersect(const UxRect& o);       // 求交集,false=无交集
};

// 脏矩形累积器:把多次失效区域合并成最小包围盒
struct DirtyRegion {
    UxRect box;
    bool   active;
    DirtyRegion() : active(false) { box.x = box.y = box.w = box.h = 0; }
    void reset() { active = false; }
    void add(const UxRect& r);
    bool intersects(const UxRect& r) const;
};

// ===================== Widget 基类 =====================
class Widget {
public:
    explicit Widget(Widget* parent = 0);
    virtual ~Widget();

    // ---- 几何(相对父控件) ----
    void set_bounds(int x_, int y_, int w_, int h_);
    void set_size(int w_, int h_);
    void move_to(int x_, int y_);
    int  preferred_w() const { return pref_w_; }
    int  preferred_h() const { return pref_h_; }
    void set_preferred(int pw, int ph) { pref_w_ = pw; pref_h_ = ph; }

    // ---- 状态 ----
    bool  is_visible() const { return visible_; }
    bool  is_enabled() const { return enabled_; }
    void  set_visible(bool v);
    void  set_enabled(bool e);
    bool  has_focus() const { return focused_; }

    // ---- 控件树 ----
    Widget* parent_node() { return parent_; }
    int     child_count() const { return children_.size(); }
    Widget* child_at(int i) const { return children_[i]; }
    void    add(Widget* c);          // 挂子控件(自动 reparent)
    void    remove(Widget* c);       // 摘离子控件(不销毁)
    Widget* child_at_point(int ax, int ay);  // 命中测试(绝对坐标),返回最顶层命中的子控件

    // ---- 布局 ----
    void set_layout(LayoutKind k, int spacing = 4);
    void set_grid_cols(int cols) { grid_cols_ = cols; }
    void set_margin(int m) { margin_ = m; }
    virtual void layout();           // 根据 layout_kind 计算所有子控件位置
    int  content_width() const { return w; }
    int  content_height() const { return h; }

    // ---- 坐标换算 ----
    int abs_x() const;               // 沿父链累加得到的绝对 x
    int abs_y() const;
    // 把绝对坐标转成本控件本地坐标
    void to_local(int ax, int ay, int& lx, int& ly) const {
        lx = ax - abs_x();
        ly = ay - abs_y();
    }

    // ---- 事件分发(绝对坐标入口) ----
    // 返回 true 表示事件被某子控件(或自身)消费
    bool dispatch_mouse(int ax, int ay, int button, bool down);
    bool dispatch_key(int keycode, int ascii, bool down);
    // 递归绘制:ox,oy 为本控件在 Buffer 中的绝对原点
    void paint_tree(gfxlib::Buffer b, int ox, int oy);

    // ---- 脏矩形 / 失效 ----
    void invalidate() { dirty_ = true; }
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

    // ---- 焦点管理 ----
    void set_focus();                // 请求焦点(沿链通知根)
    void clear_focus();
    Widget* focus_chain() { return focus_; }

    // ---- 虚函数钩子(子类覆写) ----
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy);
    virtual bool on_mouse(const UxMouseEvent& e);   // 返回 true 消费
    virtual void on_key(const UxKeyEvent& e);
    virtual const char* tip_text() const { return ""; }

    // ---- 内建 5x7 位图文字渲染(写入 Buffer) ----
    void draw_text(gfxlib::Buffer b, int x, int y, const char* s, gfxlib::Pixel fg);
    int  text_width(const char* s) const;
    int  text_height() const { return 7; }

    // ---- 公共几何字段 ----
    int x, y, w, h;

protected:
    // 便捷绘制助手(子类可直接用)
    void fill_rect(gfxlib::Buffer b, int ox, int oy, gfxlib::Pixel c) {
        gfxlib::draw_rect_fill(b, ox + x, oy + y, w, h, c);
    }
    void frame_rect(gfxlib::Buffer b, int ox, int oy, gfxlib::Pixel c) {
        gfxlib::draw_rect(b, ox + x, oy + y, w, h, c);
    }

    bool visible_;
    bool enabled_;
    bool focused_;
    bool hovered_;
    bool pressed_;

    Widget* parent_;
    List<Widget*> children_;

    LayoutKind layout_;
    int spacing_;
    int grid_cols_;
    int margin_;
    int pref_w_, pref_h_;

    Widget* focus_;      // 当前拥有焦点的子控件(仅容器关心)
    bool    dirty_;
};

// ===================== 库级自检 =====================
// 各模块返回失败数(0=全部通过)
int widget_self_test();

} // namespace ui
} // namespace nefu
