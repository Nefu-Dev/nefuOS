// nefuOS UI 组件库 —— 标准控件实现
#include "controls.h"
#include <string.h>

namespace nefu {
namespace ui {

// ===================== Button =====================
Button::Button(Widget* parent) : Widget(parent), on_click_(0), click_user_(0) {
    label_ = "Button";
    pref_w_ = 80; pref_h_ = 28;
}
void Button::set_text(const char* s) { label_ = s; invalidate(); }

void Button::click() {
    if (on_click_) on_click_(this, click_user_);
    invalidate();
}

bool Button::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) { pressed_ = true; invalidate(); return true; }
    // 释放:若仍在按钮内则触发
    pressed_ = false;
    invalidate();
    if (e.x >= 0 && e.x < w && e.y >= 0 && e.y < h) click();
    return true;
}

void Button::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::Pixel bg = pressed_ ? t.accent_dim : (enabled_ ? t.accent : t.track);
    gfxlib::draw_rect_fill(b, ox, oy, w, h, bg);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    // 文字居中
    int tw = text_width(label_.c_str());
    int tx = ox + (w - tw) / 2;
    int ty = oy + (h - text_height()) / 2;
    draw_text(b, tx, ty, label_.c_str(), t.white);
}

// ===================== Label =====================
Label::Label(Widget* parent) : Widget(parent), align_(AlignLeft) {
    color_ = theme().text;
    pref_w_ = 60; pref_h_ = 12;
}
void Label::set_text(const char* s) { text_ = s; invalidate(); }

void Label::on_paint(gfxlib::Buffer b, int ox, int oy) {
    int tw = text_width(text_.c_str());
    int tx = ox;
    if (align_ == AlignCenter) tx = ox + (w - tw) / 2;
    else if (align_ == AlignRight) tx = ox + w - tw;
    int ty = oy + (h - text_height()) / 2;
    draw_text(b, tx, ty, text_.c_str(), color_);
}

// ===================== TextField =====================
TextField::TextField(Widget* parent) : Widget(parent), cursor_(0), scroll_x_(0) {
    pref_w_ = 140; pref_h_ = 24;
}
void TextField::set_text(const char* s) {
    buf_ = s ? s : "";
    cursor_ = buf_.len();
    invalidate();
}
bool TextField::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) set_focus();
    return true;
}
void TextField::move_cursor(int d) {
    int n = buf_.len();
    cursor_ += d;
    if (cursor_ < 0) cursor_ = 0;
    if (cursor_ > n) cursor_ = n;
    invalidate();
}
void TextField::on_key(const UxKeyEvent& e) {
    if (!e.down) return;
    if (e.keycode == UX_KEY_LEFT) move_cursor(-1);
    else if (e.keycode == UX_KEY_RIGHT) move_cursor(1);
    else if (e.keycode == UX_KEY_HOME) { cursor_ = 0; invalidate(); }
    else if (e.keycode == UX_KEY_END)  { cursor_ = buf_.len(); invalidate(); }
    else if (e.keycode == UX_KEY_BACKSPACE) {
        if (cursor_ > 0) {
            // 删除光标前一字符(简单移动 buffer)
            String nb;
            for (int i = 0; i < buf_.len(); i++)
                if (i != cursor_ - 1) nb += buf_[i];
            buf_ = nb;
            cursor_--;
            invalidate();
        }
    } else if (e.ascii >= 32 && e.ascii < 127) {
        char ch = (char)e.ascii;
        String nb;
        for (int i = 0; i < buf_.len(); i++) {
            if (i == cursor_) nb += ch;
            nb += buf_[i];
        }
        if (cursor_ == buf_.len()) nb += ch;
        buf_ = nb;
        cursor_++;
        invalidate();
    }
}
void TextField::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, focused_ ? t.accent : t.border);
    const char* s = buf_.len() ? buf_.c_str() : ph_.c_str();
    gfxlib::Pixel fg = buf_.len() ? t.text : t.text_dim;
    draw_text(b, ox + 4, oy + (h - 7) / 2, s, fg);
    // 光标
    if (focused_) {
        int cx = ox + 4 + text_width(buf_.c_str() ? buf_.c_str() : "") ;
        // 光标位置按前 cursor_ 个字符估算
        cx = ox + 4 + cursor_ * 6;
        gfxlib::draw_line(b, cx, oy + 4, cx, oy + h - 4, t.accent);
    }
}

// ===================== TextArea =====================
TextArea::TextArea(Widget* parent) : Widget(parent), scroll_line_(0) {
    pref_w_ = 160; pref_h_ = 80;
}
void TextArea::rebuild_lines() {
    lines_.clear();
    String cur;
    for (int i = 0; i < buf_.len(); i++) {
        char ch = buf_[i];
        if (ch == '\n') { lines_.push(cur); cur.clear(); }
        else cur += ch;
    }
    lines_.push(cur);
}
void TextArea::set_text(const char* s) { buf_ = s ? s : ""; rebuild_lines(); invalidate(); }
bool TextArea::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) set_focus();
    return true;
}
void TextArea::on_key(const UxKeyEvent& e) {
    if (!e.down) return;
    if (e.keycode == UX_KEY_UP) { if (scroll_line_ > 0) scroll_line_--; invalidate(); }
    else if (e.keycode == UX_KEY_DOWN) {
        if (scroll_line_ < lines_.size() - 1) scroll_line_++;
        invalidate();
    } else if (e.ascii == '\n') {
        buf_ += '\n'; rebuild_lines(); invalidate();
    } else if (e.ascii >= 32 && e.ascii < 127) {
        buf_ += (char)e.ascii; rebuild_lines(); invalidate();
    }
}
void TextArea::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    int rh = 10;
    int max_lines = h / rh;
    for (int i = 0; i < max_lines && (i + scroll_line_) < lines_.size(); i++) {
        draw_text(b, ox + 4, oy + 2 + i * rh, lines_[i + scroll_line_].c_str(), t.text);
    }
}

// ===================== CheckBox =====================
CheckBox::CheckBox(Widget* parent) : Widget(parent), checked_(false), on_toggle_(0), toggle_user_(0) {
    pref_w_ = 100; pref_h_ = 20;
}
void CheckBox::set_text(const char* s) { label_ = s; invalidate(); }
void CheckBox::set_checked(bool c) {
    bool old = checked_;
    checked_ = c;
    if (old != checked_ && on_toggle_) on_toggle_(this, toggle_user_);
    invalidate();
}
bool CheckBox::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) return true;
    set_checked(!checked_);
    return true;
}
void CheckBox::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    int box = 12;
    int by = oy + (h - box) / 2;
    gfxlib::draw_rect_fill(b, ox, by, box, box, t.white);
    gfxlib::draw_rect(b, ox, by, box, box, t.accent);
    if (checked_) {
        // 画勾:两条线
        gfxlib::draw_line(b, ox + 2, by + 6, ox + 5, by + 9, t.accent);
        gfxlib::draw_line(b, ox + 5, by + 9, ox + 10, by + 3, t.accent);
    }
    draw_text(b, ox + box + 6, oy + (h - 7) / 2, label_.c_str(), t.text);
}

// ===================== RadioButton =====================
RadioButton::RadioButton(Widget* parent) : CheckBox(parent), group_(0) {
    pref_w_ = 100; pref_h_ = 20;
}
bool RadioButton::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) return true;
    // 互斥:把同组其他按钮取消
    if (group_) {
        if (group_->checked && group_->checked != this) {
            RadioButton* other = (RadioButton*)group_->checked;
            other->CheckBox::set_checked(false);
        }
        group_->checked = this;
    }
    set_checked(true);
    return true;
}
void RadioButton::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    int d = 12;
    int cy = oy + h / 2;
    gfxlib::draw_circle(b, ox + d / 2, cy, d / 2, t.accent);
    if (checked_) gfxlib::draw_circle_fill(b, ox + d / 2, cy, d / 2 - 3, t.accent);
    draw_text(b, ox + d + 6, oy + (h - 7) / 2, label_.c_str(), t.text);
}

// ===================== Switch =====================
Switch::Switch(Widget* parent) : Widget(parent), on_(false), on_toggle_(0), toggle_user_(0) {
    pref_w_ = 44; pref_h_ = 22;
}
void Switch::set_on(bool v) {
    bool old = on_;
    on_ = v;
    if (old != on_ && on_toggle_) on_toggle_(this, toggle_user_);
    invalidate();
}
bool Switch::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) { set_on(!on_); return true; }
    return true;
}
void Switch::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::Pixel track = on_ ? t.accent : t.track;
    gfxlib::draw_rect_fill(b, ox, oy + 3, w, h - 6, track);
    // 圆形手柄
    int knob = h - 6;
    int kx = on_ ? (ox + w - knob - 2) : (ox + 2);
    gfxlib::draw_circle_fill(b, kx + knob / 2, oy + h / 2, knob / 2, t.white);
}

// ===================== Slider =====================
Slider::Slider(Widget* parent) : Widget(parent), lo_(0), hi_(100), value_(50), horiz_(true),
                                  on_change_(0), change_user_(0) {
    pref_w_ = 120; pref_h_ = 20;
}
void Slider::set_range(int lo, int hi) { lo_ = lo; hi_ = hi; if (value_ < lo_) value_ = lo_; if (value_ > hi_) value_ = hi_; invalidate(); }
void Slider::set_value(int v) {
    if (v < lo_) v = lo_; if (v > hi_) v = hi_;
    if (v != value_) { value_ = v; if (on_change_) on_change_(this, change_user_); }
    invalidate();
}
int Slider::knob_position() const {
    int span = hi_ - lo_;
    if (span <= 0) span = 1;
    if (horiz_) {
        int track_w = w - 12;
        return 6 + (value_ - lo_) * track_w / span;
    } else {
        int track_h = h - 12;
        return h - 6 - (value_ - lo_) * track_h / span;
    }
}
bool Slider::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down && e.button != 0) { pressed_ = false; return true; }
    pressed_ = true;
    int span = hi_ - lo_; if (span <= 0) span = 1;
    if (horiz_) {
        int track_w = w - 12;
        int v = lo_ + (e.x - 6) * span / track_w;
        set_value(v);
    } else {
        int track_h = h - 12;
        int v = lo_ + (h - 6 - e.y) * span / track_h;
        set_value(v);
    }
    return true;
}
void Slider::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    if (horiz_) {
        gfxlib::draw_rect_fill(b, ox, oy + h / 2 - 2, w, 4, t.track);
        int kx = ox + knob_position();
        gfxlib::draw_rect_fill(b, kx - 5, oy + 2, 10, h - 4, t.accent);
    } else {
        gfxlib::draw_rect_fill(b, ox + w / 2 - 2, oy, 4, h, t.track);
        int ky = oy + knob_position();
        gfxlib::draw_rect_fill(b, ox + 2, ky - 5, w - 4, 10, t.accent);
    }
}

// ===================== ProgressBar =====================
ProgressBar::ProgressBar(Widget* parent) : Widget(parent), lo_(0), hi_(100), value_(0) {
    pref_w_ = 120; pref_h_ = 14;
}
void ProgressBar::set_range(int lo, int hi) { lo_ = lo; hi_ = hi; invalidate(); }
void ProgressBar::set_value(int v) { if (v < lo_) v = lo_; if (v > hi_) v = hi_; value_ = v; invalidate(); }
void ProgressBar::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.track);
    int span = hi_ - lo_; if (span <= 0) span = 1;
    int fillw = (value_ - lo_) * w / span;
    gfxlib::draw_rect_fill(b, ox, oy, fillw, h, t.accent);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
}

// ===================== Spinner =====================
Spinner::Spinner(Widget* parent) : Widget(parent), lo_(0), hi_(100), value_(0),
                                   on_change_(0), change_user_(0) {
    pref_w_ = 70; pref_h_ = 24;
}
void Spinner::set_range(int lo, int hi) { lo_ = lo; hi_ = hi; invalidate(); }
void Spinner::set_value(int v) {
    if (v < lo_) v = lo_; if (v > hi_) v = hi_;
    if (v != value_) { value_ = v; if (on_change_) on_change_(this, change_user_); }
    invalidate();
}
void Spinner::step_up()   { set_value(value_ + 1); }
void Spinner::step_down() { set_value(value_ - 1); }
bool Spinner::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    int half = h / 2;
    if (e.y < half) step_up(); else step_down();
    return true;
}
void Spinner::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    char buf[24];
    // 内联 sprintf(避免依赖 klib String 格式化,这里用 ksprintf)
    ksprintf(buf, sizeof(buf), "%d", value_);
    int tw = text_width(buf);
    draw_text(b, ox + 6, oy + (h - 7) / 2, buf, t.text);
    // 上下小按钮
    int bw = 16;
    gfxlib::draw_rect_fill(b, ox + w - bw, oy, bw, h / 2, t.track);
    gfxlib::draw_rect_fill(b, ox + w - bw, oy + h / 2, bw, h / 2, t.track);
    // ^ v 箭头
    gfxlib::draw_line(b, ox + w - bw + 4, oy + h / 4 + 1, ox + w - bw + bw / 2, oy + 3, t.text);
    gfxlib::draw_line(b, ox + w - bw + bw / 2, oy + 3, ox + w - bw + bw - 4, oy + h / 4 + 1, t.text);
    gfxlib::draw_line(b, ox + w - bw + 4, oy + h / 2 + h / 4 - 1, ox + w - bw + bw / 2, oy + h - 3, t.text);
    gfxlib::draw_line(b, ox + w - bw + bw / 2, oy + h - 3, ox + w - bw + bw - 4, oy + h / 2 + h / 4 - 1, t.text);
}

// ===================== ComboBox =====================
ComboBox::ComboBox(Widget* parent) : Widget(parent), sel_(-1), open_(false) {
    pref_w_ = 120; pref_h_ = 24;
}
void ComboBox::add_item(const char* s) { items_.push(String(s)); if (sel_ < 0) sel_ = 0; invalidate(); }
void ComboBox::set_selected(int i) { if (i >= -1 && i < items_.size()) sel_ = i; invalidate(); }
bool ComboBox::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (e.down) { open_ = !open_; return true; }
    if (open_) {
        // 计算点中哪一项
        int row = (e.y - h) / 16;
        if (row >= 0 && row < items_.size()) { sel_ = row; open_ = false; invalidate(); }
    }
    return true;
}
void ComboBox::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    const char* s = (sel_ >= 0 && sel_ < items_.size()) ? items_[sel_].c_str() : "";
    draw_text(b, ox + 6, oy + (h - 7) / 2, s, t.text);
    // 下拉箭头
    gfxlib::draw_line(b, ox + w - 14, oy + h / 2 - 2, ox + w - 9, oy + h / 2 + 2, t.text);
    gfxlib::draw_line(b, ox + w - 9, oy + h / 2 + 2, ox + w - 4, oy + h / 2 - 2, t.text);
    // 展开列表
    if (open_) {
        int lh = 16;
        gfxlib::draw_rect_fill(b, ox, oy + h, w, items_.size() * lh, t.panel);
        gfxlib::draw_rect(b, ox, oy + h, w, items_.size() * lh, t.border);
        for (int i = 0; i < items_.size(); i++) {
            if (i == sel_) gfxlib::draw_rect_fill(b, ox, oy + h + i * lh, w, lh, t.selection);
            draw_text(b, ox + 6, oy + h + i * lh + (lh - 7) / 2, items_[i].c_str(), t.text);
        }
    }
}

// ===================== ListBox =====================
ListBox::ListBox(Widget* parent) : Widget(parent), sel_(-1), scroll_row_(0) {
    pref_w_ = 140; pref_h_ = 100;
}
void ListBox::clear() { items_.clear(); sel_ = -1; scroll_row_ = 0; invalidate(); }
void ListBox::add_item(const char* s) { items_.push(String(s)); invalidate(); }
void ListBox::set_selected(int i) { if (i >= -1 && i < items_.size()) sel_ = i; invalidate(); }
int ListBox::visible_rows() const { return h / row_h(); }
bool ListBox::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    int row = e.y / row_h() + scroll_row_;
    if (row >= 0 && row < items_.size()) { sel_ = row; invalidate(); }
    return true;
}
void ListBox::on_key(const UxKeyEvent& e) {
    if (!e.down) return;
    if (e.keycode == UX_KEY_DOWN) { if (sel_ < items_.size() - 1) sel_++; invalidate(); }
    else if (e.keycode == UX_KEY_UP) { if (sel_ > 0) sel_--; invalidate(); }
}
void ListBox::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    int vr = visible_rows();
    for (int i = 0; i < vr && (i + scroll_row_) < items_.size(); i++) {
        int row = i + scroll_row_;
        if (row == sel_) gfxlib::draw_rect_fill(b, ox + 1, oy + 1 + i * row_h(), w - 2, row_h(), t.selection);
        draw_text(b, ox + 6, oy + 2 + i * row_h(), items_[row].c_str(), t.text);
    }
}

// ===================== TreeView =====================
TreeView::TreeView(Widget* parent) : Widget(parent) {
    root = new TreeNode();
    root->text = "root";
    pref_w_ = 160; pref_h_ = 120;
}
TreeView::~TreeView() { delete root; }
TreeNode* TreeView::add_root(const char* text) {
    TreeNode* n = new TreeNode();
    n->text = text;
    root->children.push(n);
    invalidate();
    return n;
}
TreeNode* TreeView::add_child(TreeNode* p, const char* text) {
    if (!p) p = root;
    TreeNode* n = new TreeNode();
    n->text = text;
    p->children.push(n);
    invalidate();
    return n;
}
int TreeView::collect_visible(TreeNode* n, TreeNode** out, int max) const {
    int cnt = 0;
    for (int i = 0; i < n->children.size(); i++) {
        TreeNode* c = n->children[i];
        if (out && cnt < max) out[cnt] = c;
        cnt++;
        if (c->expanded) cnt += collect_visible(c, out ? out + cnt : 0, max - cnt);
    }
    return cnt;
}
int TreeView::total_visible() const { return collect_visible(root, 0, 0); }
bool TreeView::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    int rh = 14;
    int idx = e.y / rh;
    // 找到第 idx 个可见节点并切换展开
    TreeNode** arr = new TreeNode*[64];
    int n = collect_visible(root, arr, 64);
    if (idx >= 0 && idx < n) {
        if (arr[idx]->children.size() > 0) arr[idx]->expanded = !arr[idx]->expanded;
    }
    delete[] arr;
    invalidate();
    return true;
}
void TreeView::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    TreeNode** arr = new TreeNode*[64];
    int n = collect_visible(root, arr, 64);
    int rh = 14;
    // 简单缩进绘制(深度按层数,这里简化为一级缩进)
    for (int i = 0; i < n && i < h / rh; i++) {
        TreeNode* nd = arr[i];
        int indent = 10;
        if (nd->children.size() > 0) {
            // 展开标记 +/-
            draw_text(b, ox + 4, oy + 2 + i * rh, nd->expanded ? "-" : "+", t.accent);
        }
        draw_text(b, ox + 4 + indent, oy + 2 + i * rh, nd->text.c_str(), t.text);
    }
    delete[] arr;
}

// ===================== ScrollBar =====================
ScrollBar::ScrollBar(Widget* parent) : Widget(parent),
    visible_(10), total_(100), value_(0), horiz_(false) {
    pref_w_ = 16; pref_h_ = 100;
}
void ScrollBar::set_range(int vis, int tot) {
    visible_ = vis > 0 ? vis : 1;
    total_ = tot > visible_ ? tot : visible_;
    if (value_ > total_ - visible_) value_ = total_ - visible_;
    invalidate();
}
void ScrollBar::set_value(int v) {
    int maxv = total_ - visible_; if (maxv < 0) maxv = 0;
    if (v < 0) v = 0; if (v > maxv) v = maxv;
    value_ = v; invalidate();
}
int ScrollBar::knob_size() const {
    int track = horiz_ ? w : h;
    int ks = track * visible_ / total_;
    if (ks < 12) ks = 12;
    return ks;
}
int ScrollBar::knob_pos() const {
    int track = horiz_ ? w : h;
    int ks = knob_size();
    int span = track - ks;
    int maxv = total_ - visible_; if (maxv <= 0) maxv = 1;
    return value_ * span / maxv;
}
bool ScrollBar::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    int track = horiz_ ? w : h;
    int ks = knob_size();
    int span = track - ks; if (span < 1) span = 1;
    int pos = horiz_ ? e.x : e.y;
    int maxv = total_ - visible_; if (maxv < 0) maxv = 0;
    int v = (pos - ks / 2) * maxv / span;
    set_value(v);
    return true;
}
void ScrollBar::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.track);
    int ks = knob_size();
    int kp = knob_pos();
    if (horiz_) {
        gfxlib::draw_rect_fill(b, ox + kp, oy, ks, h, t.accent);
    } else {
        gfxlib::draw_rect_fill(b, ox, oy + kp, w, ks, t.accent);
    }
}

// ===================== Separator =====================
Separator::Separator(Widget* parent) : Widget(parent), horizontal(true) {
    pref_w_ = 100; pref_h_ = 2;
}
void Separator::on_paint(gfxlib::Buffer b, int ox, int oy) {
    gfxlib::Pixel c = theme().border;
    if (horizontal) gfxlib::draw_line(b, ox, oy, ox + w, oy, c);
    else            gfxlib::draw_line(b, ox, oy, ox, oy + h, c);
}

// ===================== StatusBar =====================
StatusBar::StatusBar(Widget* parent) : Widget(parent) {
    pref_w_ = 200; pref_h_ = 20;
    fields_[0] = "Ready";
}
void StatusBar::set_text(const char* s, int idx) {
    if (idx >= 0 && idx < 3) fields_[idx] = s;
    invalidate();
}
void StatusBar::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.track);
    gfxlib::draw_line(b, ox, oy, ox + w, oy, t.border);
    int fx = ox + 6;
    for (int i = 0; i < 3; i++) {
        if (fields_[i].len() == 0) continue;
        draw_text(b, fx, oy + (h - 7) / 2, fields_[i].c_str(), t.text);
        fx += text_width(fields_[i].c_str()) + 24;
    }
}

// ===================== GroupBox =====================
GroupBox::GroupBox(Widget* parent) : Widget(parent) {
    pref_w_ = 180; pref_h_ = 100;
    title_ = "Group";
}
void GroupBox::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    gfxlib::draw_rect(b, ox, oy + 8, w, h - 8, t.border);
    // 标题块覆盖上边线
    int tw = text_width(title_.c_str());
    gfxlib::draw_rect_fill(b, ox + 6, oy, tw + 8, 12, t.white);
    draw_text(b, ox + 10, oy + 2, title_.c_str(), t.text);
}

// ===================== 模块自检 =====================
// 回调计数桩
static int g_click_count = 0;
static void btn_click_cb(void* w, void* user) {
    (void)w; (void)user;
    g_click_count++;
}
static int g_toggle_count = 0;
static void chk_toggle_cb(void* w, void* user) {
    (void)w; (void)user;
    g_toggle_count++;
}

int controls_self_test() {
    int fail = 0;

    // --- Button:点击回调被触发 ---
    {
        Button b;
        b.set_bounds(0, 0, 80, 28);
        b.set_on_click(btn_click_cb);
        int before = g_click_count;
        // 模拟按下+释放(命中)
        b.on_mouse(UxMouseEvent{ 40, 14, 0, true });
        b.on_mouse(UxMouseEvent{ 40, 14, 0, false });
        if (g_click_count != before + 1) fail++;
        // 移出按钮释放不应触发
        b.on_mouse(UxMouseEvent{ 40, 14, 0, true });
        b.on_mouse(UxMouseEvent{ 500, 500, 0, false });
        if (g_click_count != before + 1) fail++;
        // 程序化 click
        b.click();
        if (g_click_count != before + 2) fail++;
    }

    // --- Label:对齐与文本 ---
    {
        Label l;
        l.set_bounds(0, 0, 100, 14);
        l.set_text("Hi");
        if (strcmp(l.text(), "Hi") != 0) fail++;
    }

    // --- TextField:输入与退格 ---
    {
        TextField tf;
        tf.set_bounds(0, 0, 100, 24);
        tf.set_focus();
        // 输入 'A','B'
        tf.on_key(UxKeyEvent{ 0, 'A', true });
        tf.on_key(UxKeyEvent{ 0, 'B', true });
        if (strcmp(tf.text(), "AB") != 0) fail++;
        // 退格一次
        tf.on_key(UxKeyEvent{ UX_KEY_BACKSPACE, 0, true });
        if (strcmp(tf.text(), "A") != 0) fail++;
        // 方向键移动光标不应改文本
        tf.on_key(UxKeyEvent{ UX_KEY_LEFT, 0, true });
        if (strcmp(tf.text(), "A") != 0) fail++;
    }

    // --- CheckBox:切换状态与回调 ---
    {
        CheckBox c;
        c.set_bounds(0, 0, 100, 20);
        c.set_on_toggle(chk_toggle_cb);
        int before = g_toggle_count;
        if (c.is_checked()) fail++;
        c.on_mouse(UxMouseEvent{ 5, 5, 0, true });
        c.on_mouse(UxMouseEvent{ 5, 5, 0, false });
        if (!c.is_checked()) fail++;
        if (g_toggle_count != before + 1) fail++;
        // 再点取消
        c.on_mouse(UxMouseEvent{ 5, 5, 0, true });
        c.on_mouse(UxMouseEvent{ 5, 5, 0, false });
        if (c.is_checked()) fail++;
    }

    // --- RadioButton:同组互斥 ---
    {
        RadioGroup grp;
        RadioButton a, b;
        a.set_group(&grp); b.set_group(&grp);
        a.on_mouse(UxMouseEvent{ 5, 5, 0, true });
        a.on_mouse(UxMouseEvent{ 5, 5, 0, false });
        if (!a.is_checked()) fail++;
        b.on_mouse(UxMouseEvent{ 5, 5, 0, true });
        b.on_mouse(UxMouseEvent{ 5, 5, 0, false });
        if (!b.is_checked()) fail++;
        if (a.is_checked()) fail++;   // a 应被取消
    }

    // --- Switch:开关 ---
    {
        Switch s;
        s.set_bounds(0, 0, 44, 22);
        if (s.is_on()) fail++;
        s.on_mouse(UxMouseEvent{ 20, 11, 0, true });
        if (!s.is_on()) fail++;
    }

    // --- Slider:值映射到手柄位置 ---
    {
        Slider s;
        s.set_bounds(0, 0, 100, 20);
        s.set_range(0, 100);
        s.set_value(0);
        if (s.knob_position() != 6) fail++;          // 左端
        s.set_value(100);
        if (s.knob_position() != 6 + (100 - 12)) fail++; // 右端
        s.set_value(50);
        // 中间位置
        int kp = s.knob_position();
        if (kp < 40 || kp > 60) fail++;
        // 越界钳制
        s.set_value(200);
        if (s.value() != 100) fail++;
        s.set_value(-50);
        if (s.value() != 0) fail++;
    }

    // --- ProgressBar ---
    {
        ProgressBar p;
        p.set_range(0, 100);
        p.set_value(25);
        if (p.value() != 25) fail++;
    }

    // --- Spinner:增减 ---
    {
        Spinner sp;
        sp.set_range(0, 10);
        sp.set_value(3);
        sp.step_up();
        if (sp.value() != 4) fail++;
        sp.step_down(); sp.step_down();
        if (sp.value() != 2) fail++;
        sp.set_value(10); sp.step_up();   // 钳制
        if (sp.value() != 10) fail++;
    }

    // --- ComboBox:选中项 ---
    {
        ComboBox cb;
        cb.add_item("Apple");
        cb.add_item("Banana");
        cb.add_item("Cherry");
        if (cb.item_count() != 3) fail++;
        if (cb.selected() != 0) fail++;   // 默认选第一个
        cb.set_selected(2);
        if (cb.selected() != 2) fail++;
        if (strcmp(cb.item(1), "Banana") != 0) fail++;
    }

    // --- ListBox:选择与滚动 ---
    {
        ListBox lb;
        lb.set_bounds(0, 0, 100, 64);
        for (int i = 0; i < 10; i++) {
            char buf[16]; ksprintf(buf, sizeof(buf), "Item%d", i);
            lb.add_item(buf);
        }
        if (lb.item_count() != 10) fail++;
        lb.set_selected(3);
        if (lb.selected() != 3) fail++;
        if (lb.visible_rows() != 4) fail++;   // 64/16
        // 键盘向下
        lb.on_key(UxKeyEvent{ UX_KEY_DOWN, 0, true });
        if (lb.selected() != 4) fail++;
    }

    // --- TreeView:展开/折叠计数 ---
    {
        TreeView tv;
        TreeNode* a = tv.add_root("A");
        tv.add_root("B");
        tv.add_child(a, "A1");
        tv.add_child(a, "A2");
        // 默认全部折叠:可见 = 根下两个节点(A,B)
        if (tv.total_visible() != 2) fail++;
        tv.toggle(a);
        // 展开 A 后:可见 = A,B,A1,A2 = 4
        if (tv.total_visible() != 4) fail++;
    }

    // --- ScrollBar:滑块尺寸与位置映射 ---
    {
        ScrollBar sb;
        sb.set_bounds(0, 0, 16, 200);
        sb.set_range(10, 100);          // 可视10/总100
        int ks = sb.knob_size();
        if (ks <= 0 || ks >= 200) fail++;
        sb.set_value(0);
        if (sb.knob_pos() != 0) fail++;
        sb.set_value(90);               // 最大滚动位置
        if (sb.knob_pos() <= 0) fail++;
        // 越界钳制
        sb.set_value(9999);
        if (sb.value() != 90) fail++;
        // 水平方向
        sb.set_horizontal(true);
        sb.set_bounds(0, 0, 200, 16);
        if (!sb.horizontal()) fail++;
    }

    // --- Separator ---
    {
        Separator sp;
        sp.horizontal = false;
        if (sp.horizontal) fail++;   // 应为垂直
    }

    // --- StatusBar ---
    {
        StatusBar sb;
        sb.set_text("Caps Lock", 1);
        sb.set_text("Ln 1, Col 1", 2);
    }

    // --- GroupBox:容器与标题 ---
    {
        GroupBox gb;
        gb.set_bounds(0, 0, 200, 120);
        gb.set_title("Options");
        if (strcmp(gb.title(), "Options") != 0) fail++;
        if (gb.content_top() != 18) fail++;
        // 子控件挂在 GroupBox 内
        CheckBox* c = new CheckBox(&gb);
        c->set_text("Enable");
        if (gb.child_count() != 1) fail++;
    }

    return fail;
}

} // namespace ui
} // namespace nefu
