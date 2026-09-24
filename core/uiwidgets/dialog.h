// nefuOS UI 组件库 —— 对话框组件
// MessageDialog(信息/警告/错误/确认)、InputDialog、FileDialog(文件浏览)、
// ColorDialog(调色板)、ProgressDialog、自定义对话框基类
#pragma once

#include "widget.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

// 对话框按钮结果
enum DialogResult {
    DlgNone = 0,
    DlgOK,
    DlgCancel,
    DlgYes,
    DlgNo,
    DlgAbort
};

enum MsgType {
    MsgInfo = 0,
    MsgWarning,
    MsgError,
    MsgConfirm
};

// 对话框基类:居中面板 + 标题栏 + 内容区
class Dialog : public Widget {
public:
    explicit Dialog(Widget* parent = 0);
    void set_title(const char* s) { title_ = s; }
    const char* title() const { return title_.c_str(); }
    void set_modal(bool m) { modal_ = m; }
    bool modal() const { return modal_; }
    DialogResult result() const { return result_; }
    void close(DialogResult r) { result_ = r; closed_ = true; }
    bool is_closed() const { return closed_; }

    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
protected:
    String title_;
    bool modal_;
    bool closed_;
    DialogResult result_;
    int  title_h() const { return 22; }
};

// 消息对话框
class MessageDialog : public Dialog {
public:
    explicit MessageDialog(Widget* parent = 0);
    void set_message(MsgType t, const char* text);
    MsgType type() const { return mtype_; }
    const char* message() const { return msg_.c_str(); }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    MsgType mtype_;
    String msg_;
};

// 输入对话框(单行文本)
class InputDialog : public Dialog {
public:
    explicit InputDialog(Widget* parent = 0);
    void set_prompt(const char* s) { prompt_ = s; }
    void set_input(const char* s) { input_ = s; }
    const char* input() const { return input_.c_str(); }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    String prompt_;
    String input_;
};

// 文件对话框(简单目录浏览,基于抽象条目列表)
class FileDialog : public Dialog {
public:
    explicit FileDialog(Widget* parent = 0);
    void set_path(const char* p);
    const char* path() const { return path_.c_str(); }
    void add_entry(const char* name, bool is_dir);
    int  entry_count() const { return names_.size(); }
    bool entry_is_dir(int i) const { return i >= 0 && i < is_dir_.size() ? is_dir_[i] : false; }
    const char* entry(int i) const { return (i >= 0 && i < names_.size()) ? names_[i].c_str() : ""; }
    void select(int i);
    int  selected() const { return sel_; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    String path_;
    List<String> names_;
    List<bool> is_dir_;
    int sel_;
};

// 颜色对话框(调色板网格)
class ColorDialog : public Dialog {
public:
    explicit ColorDialog(Widget* parent = 0);
    int  palette_cols() const { return 8; }
    int  palette_rows() const { return 6; }
    gfxlib::Pixel selected_color() const { return picked_; }
    void pick(int idx);
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    gfxlib::Pixel picked_;
};

// 进度对话框
class ProgressDialog : public Dialog {
public:
    explicit ProgressDialog(Widget* parent = 0);
    void set_range(int lo, int hi);
    void set_value(int v);
    int  value() const { return value_; }
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
private:
    int lo_, hi_, value_;
};

// ===================== 模块自检 =====================
int dialog_self_test();

} // namespace ui
} // namespace nefu
