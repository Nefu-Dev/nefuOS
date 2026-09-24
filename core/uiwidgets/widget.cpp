// nefuOS UI 组件库 —— Widget 基类实现
#include "widget.h"
#include <string.h>

namespace nefu {
namespace ui {

// ===================== 全局主题 =====================
static const UxTheme g_theme = {
    0xFFECF0F1u,  // bg        浅灰蓝背景
    0xFFFFFFFFu,  // panel     卡片白
    0xFFBDC3C7u,  // border    银灰边框
    0xFF2C3E50u,  // text      深蓝灰文字
    0xFF7F8C8Du,  // text_dim  次要文字
    0xFF3498DBu,  // accent    品牌蓝
    0xFF2980B9u,  // accent_dim 按下深蓝
    0xFFD0D7DEu,  // track     滑轨
    0xFFAED6F1u,  // selection 选中淡蓝
    0xFFFFFFFFu   // white
};
const UxTheme& theme() { return g_theme; }

// ===================== UxRect / DirtyRegion =====================
void UxRect::unite(const UxRect& o) {
    if (o.empty()) return;
    if (empty()) { *this = o; return; }
    int x1 = x < o.x ? x : o.x;
    int y1 = y < o.y ? y : o.y;
    int x2 = (x + w) > (o.x + o.w) ? (x + w) : (o.x + o.w);
    int y2 = (y + h) > (o.y + o.h) ? (y + h) : (o.y + o.h);
    x = x1; y = y1; w = x2 - x1; h = y2 - y1;
}
bool UxRect::intersect(const UxRect& o) {
    if (empty() || o.empty()) return false;
    int x1 = x > o.x ? x : o.x;
    int y1 = y > o.y ? y : o.y;
    int x2 = (x + w) < (o.x + o.w) ? (x + w) : (o.x + o.w);
    int y2 = (y + h) < (o.y + o.h) ? (y + h) : (o.y + o.h);
    if (x2 <= x1 || y2 <= y1) return false;
    x = x1; y = y1; w = x2 - x1; h = y2 - y1;
    return true;
}
void DirtyRegion::add(const UxRect& r) {
    if (r.empty()) return;
    if (!active) { box = r; active = true; return; }
    box.unite(r);
}
bool DirtyRegion::intersects(const UxRect& r) const {
    if (!active || r.empty()) return false;
    UxRect t = box;
    return t.intersect(r);
}

// ===================== 内建 5x7 位图字体 =====================
// 每个字符 7 行,每行 5 个有效像素(bit4=最左列)。覆盖 ASCII 0x20..0x5F。
// 小写字母通过大写映射渲染(UI 演示足够)。
static const uint8_t FONT5x7[][7] = {
    /* ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* '!' */ {0x00,0x00,0x5F,0x00,0x00,0x5F,0x00},
    /* '"' */ {0x00,0x06,0x06,0x00,0x06,0x06,0x00},
    /* '#' */ {0x00,0x14,0x7F,0x14,0x7F,0x14,0x00},
    /* '$' */ {0x00,0x24,0x2A,0x7F,0x2A,0x12,0x00},
    /* '%' */ {0x00,0x23,0x13,0x08,0x64,0x62,0x00},
    /* '&' */ {0x00,0x36,0x49,0x55,0x22,0x50,0x00},
    /* ''' */ {0x00,0x00,0x07,0x00,0x07,0x00,0x00},
    /* '(' */ {0x00,0x1C,0x22,0x41,0x22,0x1C,0x00},
    /* ')' */ {0x00,0x41,0x22,0x1C,0x22,0x41,0x00},
    /* '*' */ {0x00,0x08,0x2A,0x1C,0x2A,0x08,0x00},
    /* '+' */ {0x00,0x08,0x08,0x3E,0x08,0x08,0x00},
    /* ',' */ {0x00,0x00,0x80,0x60,0x00,0x00,0x00},
    /* '-' */ {0x00,0x08,0x08,0x08,0x08,0x08,0x00},
    /* '.' */ {0x00,0x00,0x60,0x60,0x00,0x00,0x00},
    /* '/' */ {0x00,0x20,0x10,0x08,0x04,0x02,0x00},
    /* '0' */ {0x00,0x3E,0x51,0x49,0x45,0x3E,0x00},
    /* '1' */ {0x00,0x00,0x42,0x7F,0x40,0x00,0x00},
    /* '2' */ {0x00,0x42,0x61,0x51,0x49,0x46,0x00},
    /* '3' */ {0x00,0x21,0x41,0x45,0x4B,0x31,0x00},
    /* '4' */ {0x00,0x18,0x14,0x12,0x7F,0x10,0x00},
    /* '5' */ {0x00,0x27,0x45,0x45,0x45,0x39,0x00},
    /* '6' */ {0x00,0x3C,0x4A,0x49,0x49,0x30,0x00},
    /* '7' */ {0x00,0x01,0x71,0x09,0x05,0x03,0x00},
    /* '8' */ {0x00,0x36,0x49,0x49,0x49,0x36,0x00},
    /* '9' */ {0x00,0x06,0x49,0x49,0x29,0x1E,0x00},
    /* ':' */ {0x00,0x00,0x6C,0x6C,0x00,0x00,0x00},
    /* ';' */ {0x00,0x00,0x8C,0x6C,0x00,0x00,0x00},
    /* '<' */ {0x00,0x10,0x08,0x04,0x08,0x10,0x00},
    /* '=' */ {0x00,0x14,0x14,0x14,0x14,0x14,0x00},
    /* '>' */ {0x00,0x04,0x08,0x10,0x08,0x04,0x00},
    /* '?' */ {0x00,0x02,0x01,0x51,0x09,0x06,0x00},
    /* '@' */ {0x00,0x32,0x49,0x79,0x41,0x3E,0x00},
    /* 'A' */ {0x00,0x7E,0x11,0x11,0x11,0x7E,0x00},
    /* 'B' */ {0x00,0x7F,0x49,0x49,0x49,0x36,0x00},
    /* 'C' */ {0x00,0x3E,0x41,0x41,0x41,0x22,0x00},
    /* 'D' */ {0x00,0x7F,0x41,0x41,0x22,0x1C,0x00},
    /* 'E' */ {0x00,0x7F,0x49,0x49,0x49,0x41,0x00},
    /* 'F' */ {0x00,0x7F,0x09,0x09,0x09,0x01,0x00},
    /* 'G' */ {0x00,0x3E,0x41,0x49,0x49,0x7A,0x00},
    /* 'H' */ {0x00,0x7F,0x08,0x08,0x08,0x7F,0x00},
    /* 'I' */ {0x00,0x00,0x41,0x7F,0x41,0x00,0x00},
    /* 'J' */ {0x00,0x20,0x40,0x41,0x3F,0x01,0x00},
    /* 'K' */ {0x00,0x7F,0x08,0x14,0x22,0x41,0x00},
    /* 'L' */ {0x00,0x7F,0x40,0x40,0x40,0x40,0x00},
    /* 'M' */ {0x00,0x7F,0x02,0x0C,0x02,0x7F,0x00},
    /* 'N' */ {0x00,0x7F,0x04,0x08,0x10,0x7F,0x00},
    /* 'O' */ {0x00,0x3E,0x41,0x41,0x41,0x3E,0x00},
    /* 'P' */ {0x00,0x7F,0x09,0x09,0x09,0x06,0x00},
    /* 'Q' */ {0x00,0x3E,0x41,0x51,0x21,0x5E,0x00},
    /* 'R' */ {0x00,0x7F,0x09,0x19,0x29,0x46,0x00},
    /* 'S' */ {0x00,0x46,0x49,0x49,0x49,0x31,0x00},
    /* 'T' */ {0x00,0x01,0x01,0x7F,0x01,0x01,0x00},
    /* 'U' */ {0x00,0x3F,0x40,0x40,0x40,0x3F,0x00},
    /* 'V' */ {0x00,0x1F,0x20,0x40,0x20,0x1F,0x00},
    /* 'W' */ {0x00,0x3F,0x40,0x38,0x40,0x3F,0x00},
    /* 'X' */ {0x00,0x63,0x14,0x08,0x14,0x63,0x00},
    /* 'Y' */ {0x00,0x07,0x08,0x70,0x08,0x07,0x00},
    /* 'Z' */ {0x00,0x61,0x51,0x49,0x45,0x43,0x00},
};
static const int FONT_FIRST = 0x20;   // 表从空格开始
static const int FONT_LAST  = 0x5A;   // 到 'Z'

static const uint8_t* glyph_for(char c) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');   // 小写转大写
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    int idx = (unsigned char)c - FONT_FIRST;
    if (idx < 0 || idx >= (int)(sizeof(FONT5x7) / sizeof(FONT5x7[0])))
        idx = '?' - FONT_FIRST;
    return FONT5x7[idx];
}

int Widget::text_width(const char* s) const {
    if (!s) return 0;
    return (int)strlen(s) * 6 - 1;   // 5 列 + 1 间距
}

void Widget::draw_text(gfxlib::Buffer b, int px, int py, const char* s, gfxlib::Pixel fg) {
    if (!s) return;
    int cx = px;
    for (; *s; s++) {
        const uint8_t* g = glyph_for(*s);
        for (int row = 0; row < 7; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    gfxlib::draw_pixel(b, cx + col, py + row, fg);
                }
            }
        }
        cx += 6;
    }
}

// ===================== 构造 / 析构 =====================
Widget::Widget(Widget* parent_)
    : x(0), y(0), w(10), h(10),
      visible_(true), enabled_(true), focused_(false),
      hovered_(false), pressed_(false),
      parent_(parent_),
      layout_(LayNone), spacing_(4), grid_cols_(1), margin_(0),
      pref_w_(60), pref_h_(24), focus_(0), dirty_(true) {
    if (parent_) parent_->add(this);
}

Widget::~Widget() {
    // 先摘掉自己
    if (parent_) parent_->remove(this);
    // 销毁所有子控件(组合关系:父拥有子)
    for (int i = 0; i < children_.size(); i++) {
        children_[i]->parent_ = 0;   // 防止子析构时再次 remove
        delete children_[i];
    }
    children_.clear();
}

// ===================== 几何 / 状态 =====================
void Widget::set_bounds(int x_, int y_, int w_, int h_) {
    x = x_; y = y_;
    w = w_ > 0 ? w_ : 1;
    h = h_ > 0 ? h_ : 1;
    invalidate();
}
void Widget::set_size(int w_, int h_) { set_bounds(x, y, w_, h_); }
void Widget::move_to(int x_, int y_)  { set_bounds(x_, y_, w, h); }

void Widget::set_visible(bool v) { visible_ = v; invalidate(); }
void Widget::set_enabled(bool e) { enabled_ = e; if (!e) pressed_ = false; invalidate(); }

int Widget::abs_x() const {
    int ax = x;
    for (Widget* p = parent_; p; p = p->parent_) ax += p->x;
    return ax;
}
int Widget::abs_y() const {
    int ay = y;
    for (Widget* p = parent_; p; p = p->parent_) ay += p->y;
    return ay;
}

// ===================== 控件树 =====================
void Widget::add(Widget* c) {
    if (!c) return;
    if (c->parent_) c->parent_->remove(c);
    c->parent_ = this;
    children_.push(c);
    invalidate();
}

void Widget::remove(Widget* c) {
    if (!c) return;
    for (int i = 0; i < children_.size(); i++) {
        if (children_[i] == c) {
            children_.remove(i);
            c->parent_ = 0;
            invalidate();
            return;
        }
    }
}

Widget* Widget::child_at_point(int ax, int ay) {
    // 自顶向下先命中自己,再按 z 序(后加入在上)逆序找子控件
    if (ax < abs_x() || ax >= abs_x() + w || ay < abs_y() || ay >= abs_y() + h)
        return 0;
    for (int i = children_.size() - 1; i >= 0; i--) {
        Widget* c = children_[i];
        if (!c->visible_) continue;
        Widget* hit = c->child_at_point(ax, ay);
        if (hit) return hit;
    }
    return this;   // 命中自身
}

// ===================== 布局引擎 =====================
void Widget::set_layout(LayoutKind k, int spacing) {
    layout_ = k;
    spacing_ = spacing;
    if (k == LayGrid && grid_cols_ < 1) grid_cols_ = 1;
}

void Widget::layout() {
    if (layout_ == LayNone) {
        // 绝对定位:子控件位置保持不变,仅递归
        for (int i = 0; i < children_.size(); i++) children_[i]->layout();
        return;
    }
    int cx = margin_, cy = margin_;
    int avail_w = w - margin_ * 2;
    if (layout_ == LayVBox) {
        for (int i = 0; i < children_.size(); i++) {
            Widget* c = children_[i];
            c->x = margin_;
            c->y = cy;
            c->w = avail_w;
            // 高度:若子控件给了偏好高则用之,否则均分
            c->h = c->pref_h_ > 0 ? c->pref_h_ : 24;
            cy += c->h + spacing_;
            c->layout();
        }
    } else if (layout_ == LayHBox) {
        int n = children_.size();
        int cell = n > 0 ? avail_w / n : avail_w;
        for (int i = 0; i < children_.size(); i++) {
            Widget* c = children_[i];
            c->x = cx;
            c->y = margin_;
            c->w = cell - spacing_;
            c->h = h - margin_ * 2;
            cx += cell;
            c->layout();
        }
    } else if (layout_ == LayGrid) {
        int cols = grid_cols_ > 0 ? grid_cols_ : 1;
        int cell_w = avail_w / cols;
        int row = 0;
        for (int i = 0; i < children_.size(); i++) {
            Widget* c = children_[i];
            int col = i % cols;
            row = i / cols;
            c->x = margin_ + col * cell_w;
            c->y = margin_ + row * (24 + spacing_);
            c->w = cell_w - spacing_;
            c->h = 24;
            c->layout();
        }
    }
    invalidate();
}

// ===================== 焦点 =====================
void Widget::set_focus() {
    Widget* root = this;
    while (root->parent_) root = root->parent_;
    // 在根上清空所有焦点,再把焦点设给 this
    // (简化实现:沿父链把 focus_ 指向对应孩子)
    Widget* cur = this;
    Widget* p = parent_;
    while (p) {
        p->focus_ = cur;
        cur = p;
        p = p->parent_;
    }
    focused_ = true;
    invalidate();
}

void Widget::clear_focus() {
    focused_ = false;
    invalidate();
}

// ===================== 事件分发 =====================
bool Widget::dispatch_mouse(int ax, int ay, int button, bool down) {
    if (!visible_ || !enabled_) return false;
    Widget* target = child_at_point(ax, ay);
    if (!target) return false;
    // 若点到了某个叶子之外的空白,target 是本容器;优先让叶子消费
    // 找到真正的最深命中:child_at_point 已递归到底
    int lx, ly;
    target->to_local(ax, ay, lx, ly);
    UxMouseEvent e = { lx, ly, button, down };
    bool consumed = target->on_mouse(e);
    (void)consumed;
    return true;
}

bool Widget::dispatch_key(int keycode, int ascii, bool down) {
    if (!visible_ || !enabled_) return false;
    // 优先把键盘事件交给当前焦点子控件
    if (focus_ && focus_->visible_ && focus_->enabled_) {
        if (focus_->dispatch_key(keycode, ascii, down)) return true;
    }
    UxKeyEvent e = { keycode, ascii, down };
    on_key(e);
    return true;
}

void Widget::paint_tree(gfxlib::Buffer b, int ox, int oy) {
    if (!visible_) return;
    on_paint(b, ox, oy);
    for (int i = 0; i < children_.size(); i++) {
        Widget* c = children_[i];
        c->paint_tree(b, ox + c->x, oy + c->y);
    }
}

// ===================== 默认绘制 =====================
void Widget::on_paint(gfxlib::Buffer b, int ox, int oy) {
    (void)b; (void)ox; (void)oy;
    // 基类不画任何东西(容器由子类或应用决定背景)
}

bool Widget::on_mouse(const UxMouseEvent& e) {
    (void)e;
    return false;
}

void Widget::on_key(const UxKeyEvent& e) {
    (void)e;
}

// ===================== 模块自检 =====================
int widget_self_test() {
    int fail = 0;

    // --- 1. 几何与父子链 ---
    {
        Widget root;
        root.set_bounds(0, 0, 400, 300);
        Widget* child = new Widget(&root);
        child->set_bounds(10, 20, 50, 30);
        if (root.abs_x() != 0 || root.abs_y() != 0) fail++;
        if (child->abs_x() != 10 || child->abs_y() != 20) fail++;
        // 嵌套两层
        Widget* gc = new Widget(child);
        gc->set_bounds(5, 5, 10, 10);
        if (gc->abs_x() != 15 || gc->abs_y() != 25) fail++;
        // 命中测试
        Widget* hit = root.child_at_point(12, 22);   // 落在 child 内
        if (hit != child) fail++;
        hit = root.child_at_point(200, 200);         // 空白落在 root
        if (hit != &root) fail++;
        delete child;   // 递归销毁 gc
    }

    // --- 2. VBox 布局:三个子控件 y 坐标依次排列 ---
    {
        Widget root;
        root.set_bounds(0, 0, 200, 200);
        root.set_layout(LayVBox, 4);
        root.set_margin(6);
        Widget* a = new Widget(&root); a->set_preferred(0, 20);
        Widget* b = new Widget(&root); b->set_preferred(0, 30);
        Widget* c = new Widget(&root); c->set_preferred(0, 16);
        root.layout();
        // margin=6, spacing=4
        if (a->y != 6) fail++;
        if (b->y != 6 + 20 + 4) fail++;          // 30
        if (c->y != 6 + 20 + 4 + 30 + 4) fail++; // 64
        if (a->x != 6) fail++;
        // root 为栈对象,离开作用域自动析构并回收子控件
    }

    // --- 3. HBox 布局:水平均分 ---
    {
        Widget root;
        root.set_bounds(0, 0, 300, 40);
        root.set_layout(LayHBox, 0);
        root.set_margin(0);
        Widget* a = new Widget(&root);
        Widget* b = new Widget(&root);
        Widget* c = new Widget(&root);
        root.layout();
        int cell = 300 / 3;
        if (a->x != 0) fail++;
        if (b->x != cell) fail++;
        if (c->x != cell * 2) fail++;
        // root 为栈对象,离开作用域自动析构并回收子控件
    }

    // --- 4. Grid 布局:按列换行 ---
    {
        Widget root;
        root.set_bounds(0, 0, 120, 100);
        root.set_layout(LayGrid, 2);
        root.set_grid_cols(3);
        root.set_margin(0);
        Widget* items[5];
        for (int i = 0; i < 5; i++) items[i] = new Widget(&root);
        root.layout();
        // 3 列:第 4 个(i=3)应换到第二行
        if (items[3]->y <= items[0]->y) fail++;
        if (items[3]->x != items[0]->x) fail++;
        // root 为栈对象,离开作用域自动析构并回收子控件
    }

    // --- 5. 坐标换算 ---
    {
        Widget root; root.set_bounds(0, 0, 100, 100);
        Widget* a = new Widget(&root); a->set_bounds(10, 10, 40, 40);
        int lx, ly;
        a->to_local(25, 30, lx, ly);
        if (lx != 15 || ly != 20) fail++;
        // root 为栈对象,离开作用域自动析构并回收子控件
    }

    // --- 6. 文本尺寸 ---
    {
        Widget root;
        if (root.text_width("ABC") != 3 * 6 - 1) fail++;
        if (root.text_width(0) != 0) fail++;
        // root 为栈对象,离开作用域自动析构并回收子控件
    }

    // --- 7. 可见/使能 状态切换 ---
    {
        Widget root;
        Widget* c = new Widget(&root);
        c->set_enabled(false);
        if (c->is_enabled()) fail++;
        c->set_visible(false);
        if (c->is_visible()) fail++;
        // root 为栈对象,离开作用域自动析构并回收子控件
    }

    // --- 8. UxRect 并集/交集/包含 ---
    {
        UxRect a = { 0, 0, 100, 100 };
        UxRect b = { 50, 50, 100, 100 };
        a.unite(b);
        // 并集应覆盖 (0,0)..(150,150)
        if (a.x != 0 || a.y != 0 || a.w != 150 || a.h != 150) fail++;
        UxRect c = { 0, 0, 100, 100 };
        UxRect d = { 50, 50, 100, 100 };
        if (!c.intersect(d)) fail++;
        // 交集 (50,50,50,50)
        if (c.x != 50 || c.y != 50 || c.w != 50 || c.h != 50) fail++;
        // 无交集
        UxRect e = { 0, 0, 10, 10 };
        UxRect f = { 100, 100, 10, 10 };
        if (e.intersect(f)) fail++;
        // 包含测试
        UxRect g = { 0, 0, 100, 100 };
        if (!g.contains(50, 50)) fail++;
        if (g.contains(100, 100)) fail++;   // 半开区间
    }

    // --- 9. DirtyRegion 累积 ---
    {
        DirtyRegion dr;
        UxRect a = { 0, 0, 10, 10 };
        UxRect b = { 50, 50, 20, 20 };
        dr.add(a);
        if (!dr.active) fail++;
        dr.add(b);
        // 合并后应覆盖 (0,0,70,70)
        if (dr.box.w != 70 || dr.box.h != 70) fail++;
        // 命中测试
        UxRect hit = { 5, 5, 2, 2 };
        if (!dr.intersects(hit)) fail++;
        UxRect miss = { 200, 200, 5, 5 };
        if (dr.intersects(miss)) fail++;
        dr.reset();
        if (dr.active) fail++;
    }

    // --- 10. 子控件计数与查找 ---
    {
        Widget root;
        new Widget(&root);
        new Widget(&root);
        new Widget(&root);
        if (root.child_count() != 3) fail++;
        if (root.child_at(1)->parent_node() != &root) fail++;
    }

    return fail;
}

} // namespace ui
} // namespace nefu
