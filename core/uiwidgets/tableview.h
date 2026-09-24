// nefuOS UI 组件库 —— TableView 表格组件
// 行列管理、单元格文字、表头、选择(单元格/行/列)、排序、滚动、列宽拖拽、虚拟滚动
#pragma once

#include "widget.h"
#include "klib/klib.h"

namespace nefu {
namespace ui {

// 选择模式
enum SelectMode { SelCell = 0, SelRow, SelColumn };

class TableView : public Widget {
public:
    explicit TableView(Widget* parent = 0);
    ~TableView();

    // ---- 行列管理 ----
    void set_columns(const char** names, int n);     // 设置表头列名
    int  col_count() const { return ncols_; }
    int  row_count() const { return nrows_; }
    void clear_rows();
    void add_row(const char** cells, int n);        // 追加一行(n 应等于列数)
    void insert_row(int at, const char** cells, int n);
    void remove_row(int at);

    // ---- 单元格访问 ----
    const char* cell(int row, int col) const;
    void  set_cell(int row, int col, const char* text);
    int   cell_color_index(int row, int col) const; // 行交替色(自检用)

    // ---- 选择 ----
    void set_select_mode(SelectMode m) { sel_mode_ = m; }
    void select_cell(int row, int col);
    int  selected_row() const { return sel_row_; }
    int  selected_col() const { return sel_col_; }

    // ---- 列宽 ----
    int  col_width(int c) const;
    void set_col_width(int c, int w_);
    int  col_at_x(int x) const;          // 本地 x -> 列索引(自检/拖拽用)

    // ---- 排序 ----
    void sort_by_col(int col, bool ascending = true);
    int  sorted_col() const { return sort_col_; }

    // ---- 滚动 / 虚拟 ----
    int  scroll_row() const { return scroll_row_; }
    void set_scroll_row(int r);
    int  visible_rows() const;           // 可视区域能显示多少数据行

    // ---- 绘制 / 事件 ----
    virtual void on_paint(gfxlib::Buffer b, int ox, int oy) override;
    virtual bool on_mouse(const UxMouseEvent& e) override;
    virtual void on_key(const UxKeyEvent& e) override;

private:
    int  header_h() const { return 18; }
    int  row_h() const { return 16; }
    int  flat_index(int r, int c) const { return r * ncols_ + c; }
    void realloc_cells();

    int ncols_;
    int nrows_;
    List<String> headers_;
    List<String> cells_;          // 扁平: row*ncols_+col
    List<int>    colw_;           // 每列宽

    SelectMode sel_mode_;
    int sel_row_, sel_col_;
    int scroll_row_;
    int sort_col_;
    bool sort_asc_;
};

// ===================== 模块自检 =====================
int tableview_self_test();

} // namespace ui
} // namespace nefu
