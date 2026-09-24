// nefuOS UI 组件库 —— 对话框实现
#include "dialog.h"
#include <string.h>

namespace nefu {
namespace ui {

// ===================== Dialog 基类 =====================
Dialog::Dialog(Widget* parent) : Widget(parent), modal_(true), closed_(false), result_(DlgNone) {
    pref_w_ = 320; pref_h_ = 180;
}
void Dialog::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    // 面板
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.panel);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    // 标题栏
    gfxlib::draw_rect_fill(b, ox, oy, w, title_h(), t.accent);
    draw_text(b, ox + 8, oy + (title_h() - 7) / 2, title_.c_str(), t.white);
    // 关闭按钮(X)
    gfxlib::draw_rect_fill(b, ox + w - 18, oy + 4, 14, 14, t.accent_dim);
    draw_text(b, ox + w - 15, oy + 5, "x", t.white);
}

// ===================== MessageDialog =====================
MessageDialog::MessageDialog(Widget* parent) : Dialog(parent), mtype_(MsgInfo) {
    pref_w_ = 300; pref_h_ = 140;
}
void MessageDialog::set_message(MsgType t, const char* text) {
    mtype_ = t; msg_ = text;
    switch (t) {
        case MsgInfo:    title_ = "Information"; break;
        case MsgWarning: title_ = "Warning"; break;
        case MsgError:   title_ = "Error"; break;
        case MsgConfirm: title_ = "Confirm"; break;
    }
    invalidate();
}
void MessageDialog::on_paint(gfxlib::Buffer b, int ox, int oy) {
    Dialog::on_paint(b, ox, oy);
    const UxTheme& t = theme();
    // 图标色块
    gfxlib::Pixel iconc = t.accent;
    if (mtype_ == MsgWarning) iconc = 0xFFF39C12u;
    if (mtype_ == MsgError)   iconc = 0xFFE74C3Cu;
    gfxlib::draw_circle_fill(b, ox + 24, oy + title_h() + 22, 14, iconc);
    draw_text(b, ox + 20, oy + title_h() + 18, "!", t.white);
    // 消息文本
    draw_text(b, ox + 48, oy + title_h() + 18, msg_.c_str(), t.text);
    // 底部 OK / Cancel 按钮区
    gfxlib::draw_rect_fill(b, ox + w - 150, oy + h - 34, 60, 24, t.accent);
    draw_text(b, ox + w - 130, oy + h - 27, "OK", t.white);
    gfxlib::draw_rect_fill(b, ox + w - 80, oy + h - 34, 60, 24, t.track);
    draw_text(b, ox + w - 64, oy + h - 27, "Cancel", t.text);
}

// ===================== InputDialog =====================
InputDialog::InputDialog(Widget* parent) : Dialog(parent) {
    pref_w_ = 320; pref_h_ = 140;
    title_ = "Input";
}
void InputDialog::on_paint(gfxlib::Buffer b, int ox, int oy) {
    Dialog::on_paint(b, ox, oy);
    const UxTheme& t = theme();
    draw_text(b, ox + 12, oy + title_h() + 12, prompt_.c_str(), t.text);
    // 输入框
    gfxlib::draw_rect_fill(b, ox + 12, oy + title_h() + 30, w - 24, 22, t.white);
    gfxlib::draw_rect(b, ox + 12, oy + title_h() + 30, w - 24, 22, t.border);
    draw_text(b, ox + 16, oy + title_h() + 36, input_.c_str(), t.text);
    // 按钮
    gfxlib::draw_rect_fill(b, ox + w - 150, oy + h - 34, 60, 24, t.accent);
    draw_text(b, ox + w - 132, oy + h - 27, "OK", t.white);
}

// ===================== FileDialog =====================
FileDialog::FileDialog(Widget* parent) : Dialog(parent), sel_(-1) {
    pref_w_ = 360; pref_h_ = 260;
    title_ = "Open File";
}
void FileDialog::set_path(const char* p) { path_ = p; names_.clear(); is_dir_.clear(); sel_ = -1; invalidate(); }
void FileDialog::add_entry(const char* name, bool is_dir) {
    names_.push(String(name));
    is_dir_.push(is_dir);
    invalidate();
}
void FileDialog::select(int i) {
    if (i >= 0 && i < names_.size()) sel_ = i;
    invalidate();
}
void FileDialog::on_paint(gfxlib::Buffer b, int ox, int oy) {
    Dialog::on_paint(b, ox, oy);
    const UxTheme& t = theme();
    // 路径条
    draw_text(b, ox + 12, oy + title_h() + 6, path_.c_str(), t.text_dim);
    // 列表
    int ly = oy + title_h() + 20;
    gfxlib::draw_rect_fill(b, ox + 12, ly, w - 24, h - title_h() - 60, t.white);
    gfxlib::draw_rect(b, ox + 12, ly, w - 24, h - title_h() - 60, t.border);
    for (int i = 0; i < names_.size() && i < 12; i++) {
        if (i == sel_) gfxlib::draw_rect_fill(b, ox + 13, ly + 1 + i * 16, w - 26, 16, t.selection);
        draw_text(b, ox + 18, ly + 3 + i * 16, entry(i), entry_is_dir(i) ? t.accent : t.text);
    }
}

// ===================== ColorDialog =====================
ColorDialog::ColorDialog(Widget* parent) : Dialog(parent), picked_(0xFFFFFFFFu) {
    pref_w_ = 220; pref_h_ = 200;
    title_ = "Pick Color";
}
void ColorDialog::pick(int idx) {
    // 由索引生成调色板颜色(HSL 均匀分布)
    int cols = palette_cols();
    int hue = (idx * 360) / (cols * palette_rows());
    gfxlib::HSL hsl; hsl.h = hue; hsl.s = 80; hsl.l = 55;
    picked_ = gfxlib::rgb_pack(gfxlib::hsl_to_rgb(hsl));
    invalidate();
}
void ColorDialog::on_paint(gfxlib::Buffer b, int ox, int oy) {
    Dialog::on_paint(b, ox, oy);
    int cell = 20;
    int x0 = ox + 16, y0 = oy + title_h() + 12;
    int idx = 0;
    for (int r = 0; r < palette_rows(); r++) {
        for (int c = 0; c < palette_cols(); c++) {
            int hue = (idx * 360) / (palette_cols() * palette_rows());
            gfxlib::HSL hsl; hsl.h = hue; hsl.s = 80; hsl.l = 55;
            gfxlib::Pixel col = gfxlib::rgb_pack(gfxlib::hsl_to_rgb(hsl));
            gfxlib::draw_rect_fill(b, x0 + c * cell, y0 + r * cell, cell - 2, cell - 2, col);
            idx++;
        }
    }
    // 选中色块
    gfxlib::draw_rect(b, ox + 16, oy + h - 40, 30, 24, theme().border);
    gfxlib::draw_rect_fill(b, ox + 17, oy + h - 39, 28, 22, picked_);
}

// ===================== ProgressDialog =====================
ProgressDialog::ProgressDialog(Widget* parent) : Dialog(parent), lo_(0), hi_(100), value_(0) {
    pref_w_ = 320; pref_h_ = 120;
    title_ = "Progress";
}
void ProgressDialog::set_range(int lo, int hi) { lo_ = lo; hi_ = hi; invalidate(); }
void ProgressDialog::set_value(int v) { if (v < lo_) v = lo_; if (v > hi_) v = hi_; value_ = v; invalidate(); }
void ProgressDialog::on_paint(gfxlib::Buffer b, int ox, int oy) {
    Dialog::on_paint(b, ox, oy);
    const UxTheme& t = theme();
    int span = hi_ - lo_; if (span <= 0) span = 1;
    int fillw = (value_ - lo_) * (w - 40) / span;
    gfxlib::draw_rect_fill(b, ox + 20, oy + h / 2, w - 40, 16, t.track);
    gfxlib::draw_rect_fill(b, ox + 20, oy + h / 2, fillw, 16, t.accent);
    char buf[24];
    ksprintf(buf, sizeof(buf), "%d%%", (value_ - lo_) * 100 / span);
    draw_text(b, ox + w / 2 - 10, oy + h / 2 + 20, buf, t.text_dim);
}

// ===================== 模块自检 =====================
int dialog_self_test() {
    int fail = 0;

    // --- MessageDialog:类型与标题 ---
    {
        MessageDialog d;
        d.set_message(MsgError, "Something broke");
        if (d.type() != MsgError) fail++;
        if (strcmp(d.title(), "Error") != 0) fail++;
        if (strcmp(d.message(), "Something broke") != 0) fail++;
        d.set_message(MsgInfo, "Hello");
        if (d.type() != MsgInfo) fail++;
    }

    // --- InputDialog:输入存取 ---
    {
        InputDialog d;
        d.set_prompt("Name:");
        d.set_input("nefu");
        if (strcmp(d.input(), "nefu") != 0) fail++;
    }

    // --- FileDialog:条目浏览 ---
    {
        FileDialog d;
        d.set_path("/home/user");
        if (strcmp(d.path(), "/home/user") != 0) fail++;
        d.add_entry("..", true);
        d.add_entry("docs", true);
        d.add_entry("readme.txt", false);
        if (d.entry_count() != 3) fail++;
        if (!d.entry_is_dir(1)) fail++;
        if (d.entry_is_dir(2)) fail++;
        d.select(2);
        if (d.selected() != 2) fail++;
    }

    // --- ColorDialog:调色板尺寸与取色 ---
    {
        ColorDialog d;
        if (d.palette_cols() * d.palette_rows() != 48) fail++;
        d.pick(0);
        // pick 后应得到一个非白颜色
        if (d.selected_color() == 0xFFFFFFFFu) fail++;
        d.pick(24);
        if (d.selected_color() == 0) fail++;
    }

    // --- ProgressDialog ---
    {
        ProgressDialog d;
        d.set_range(0, 200);
        d.set_value(100);
        if (d.value() != 100) fail++;
        d.set_value(9999);
        if (d.value() != 200) fail++;
    }

    // --- Dialog 基类:模态/关闭结果 ---
    {
        Dialog d;
        d.set_title("My Dialog");
        if (strcmp(d.title(), "My Dialog") != 0) fail++;
        if (!d.modal()) fail++;
        d.close(DlgOK);
        if (d.result() != DlgOK) fail++;
        if (!d.is_closed()) fail++;
    }

    // --- MessageDialog 四种类型标题 ---
    {
        MessageDialog d;
        d.set_message(MsgWarning, "warn");
        if (strcmp(d.title(), "Warning") != 0) fail++;
        d.set_message(MsgError, "err");
        if (strcmp(d.title(), "Error") != 0) fail++;
        d.set_message(MsgConfirm, "q?");
        if (strcmp(d.title(), "Confirm") != 0) fail++;
    }

    // --- FileDialog 路径切换清空条目 ---
    {
        FileDialog d;
        d.set_path("/a");
        d.add_entry("x", true);
        if (d.entry_count() != 1) fail++;
        d.set_path("/b");
        if (d.entry_count() != 0) fail++;
    }

    // --- ProgressDialog 百分比边界 ---
    {
        ProgressDialog d;
        d.set_range(0, 100);
        d.set_value(0);
        if (d.value() != 0) fail++;
        d.set_value(-50);
        if (d.value() != 0) fail++;
    }

    return fail;
}

} // namespace ui
} // namespace nefu
