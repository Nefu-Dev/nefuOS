// nefuOS UI 组件库 —— TableView 实现
#include "tableview.h"
#include <string.h>

namespace nefu {
namespace ui {

TableView::TableView(Widget* parent) : Widget(parent),
    ncols_(0), nrows_(0), sel_mode_(SelRow), sel_row_(-1), sel_col_(-1),
    scroll_row_(0), sort_col_(-1), sort_asc_(true) {
    pref_w_ = 240; pref_h_ = 160;
}

TableView::~TableView() {}

void TableView::set_columns(const char** names, int n) {
    headers_.clear();
    colw_.clear();
    for (int i = 0; i < n; i++) {
        headers_.push(String(names[i]));
        colw_.push(80);          // 默认列宽 80
    }
    ncols_ = n;
    realloc_cells();
    invalidate();
}

void TableView::realloc_cells() {
    int need = nrows_ * ncols_;
    while (cells_.size() < need) cells_.push(String(""));
    invalidate();
}

void TableView::clear_rows() {
    nrows_ = 0;
    cells_.clear();
    scroll_row_ = 0;
    sel_row_ = -1; sel_col_ = -1;
    invalidate();
}

void TableView::add_row(const char** rowcells, int n) {
    insert_row(nrows_, rowcells, n);
}

void TableView::insert_row(int at, const char** rowcells, int n) {
    if (at < 0) at = 0;
    if (at > nrows_) at = nrows_;
    // 扩展扁平数组:在位置 at*ncols_ 插入 n 个空串
    for (int i = 0; i < n; i++) {
        cells_.insert((at + i) * ncols_, String(""));
    }
    nrows_++;
    // 填入数据
    for (int c = 0; c < n && c < ncols_; c++) {
        cells_[at * ncols_ + c] = rowcells[c];
    }
    invalidate();
}

void TableView::remove_row(int at) {
    if (at < 0 || at >= nrows_) return;
    for (int i = 0; i < ncols_; i++) {
        cells_.remove(at * ncols_);   // 连续删 ncols_ 次(每次删最前一格)
    }
    nrows_--;
    if (sel_row_ >= nrows_) sel_row_ = nrows_ - 1;
    invalidate();
}

const char* TableView::cell(int r, int c) const {
    if (r < 0 || r >= nrows_ || c < 0 || c >= ncols_) return "";
    return cells_[r * ncols_ + c].c_str();
}

void TableView::set_cell(int r, int c, const char* text) {
    if (r < 0 || r >= nrows_ || c < 0 || c >= ncols_) return;
    cells_[r * ncols_ + c] = text;
    invalidate();
}

int TableView::cell_color_index(int r, int c) const {
    (void)c;
    return r & 1;   // 奇偶行交替
}

void TableView::select_cell(int r, int c) {
    sel_row_ = r; sel_col_ = c;
    if (sel_row_ < 0) sel_row_ = -1;
    invalidate();
}

int TableView::col_width(int c) const {
    return (c >= 0 && c < colw_.size()) ? colw_[c] : 80;
}
void TableView::set_col_width(int c, int w_) {
    if (c >= 0 && c < colw_.size() && w_ >= 20) colw_[c] = w_;
    invalidate();
}
int TableView::col_at_x(int x) const {
    int acc = 0;
    for (int c = 0; c < ncols_; c++) {
        acc += col_width(c);
        if (x < acc) return c;
    }
    return ncols_ - 1;
}

void TableView::sort_by_col(int col, bool ascending) {
    if (col < 0 || col >= ncols_ || nrows_ < 2) return;
    sort_col_ = col; sort_asc_ = ascending;
    // 插入排序(按该列字符串比较)
    for (int i = 1; i < nrows_; i++) {
        // 暂存第 i 行
        String* tmp = new String[ncols_];
        for (int c = 0; c < ncols_; c++) tmp[c] = cells_[i * ncols_ + c];
        int j = i - 1;
        while (j >= 0) {
            int cmp = strcmp(cells_[j * ncols_ + col].c_str(), tmp[col].c_str());
            if (!ascending) cmp = -cmp;
            if (cmp <= 0) break;
            for (int c = 0; c < ncols_; c++)
                cells_[(j + 1) * ncols_ + c] = cells_[j * ncols_ + c];
            j--;
        }
        for (int c = 0; c < ncols_; c++) cells_[(j + 1) * ncols_ + c] = tmp[c];
        delete[] tmp;
    }
    invalidate();
}

int TableView::visible_rows() const {
    int body = h - header_h();
    int vr = body / row_h();
    return vr < 0 ? 0 : vr;
}
void TableView::set_scroll_row(int r) {
    int maxr = nrows_ - visible_rows();
    if (maxr < 0) maxr = 0;
    if (r < 0) r = 0;
    if (r > maxr) r = maxr;
    scroll_row_ = r;
    invalidate();
}

bool TableView::on_mouse(const UxMouseEvent& e) {
    if (!enabled_) return false;
    if (!e.down) return true;
    if (e.y < header_h()) {
        // 点表头:按该列排序
        int c = col_at_x(e.x);
        sort_by_col(c, true);
        return true;
    }
    int r = (e.y - header_h()) / row_h() + scroll_row_;
    int c = col_at_x(e.x);
    if (r >= 0 && r < nrows_) { select_cell(r, c); }
    return true;
}

void TableView::on_key(const UxKeyEvent& e) {
    if (!e.down) return;
    if (e.keycode == UX_KEY_DOWN) { if (sel_row_ < nrows_ - 1) { sel_row_++; invalidate(); } }
    else if (e.keycode == UX_KEY_UP)   { if (sel_row_ > 0) { sel_row_--; invalidate(); } }
    else if (e.keycode == UX_KEY_NEXT) { set_scroll_row(scroll_row_ + visible_rows()); }
    else if (e.keycode == UX_KEY_PRIOR){ set_scroll_row(scroll_row_ - visible_rows()); }
}

void TableView::on_paint(gfxlib::Buffer b, int ox, int oy) {
    const UxTheme& t = theme();
    // 背景
    gfxlib::draw_rect_fill(b, ox, oy, w, h, t.white);
    gfxlib::draw_rect(b, ox, oy, w, h, t.border);
    // 表头
    gfxlib::draw_rect_fill(b, ox, oy, w, header_h(), t.track);
    int cx = 0;
    for (int c = 0; c < ncols_; c++) {
        int cw = col_width(c);
        draw_text(b, ox + cx + 4, oy + (header_h() - 7) / 2, headers_[c].c_str(), t.text);
        gfxlib::draw_line(b, ox + cx, oy, ox + cx, oy + header_h(), t.border);
        cx += cw;
    }
    // 数据行(虚拟滚动:只画可见行)
    int vr = visible_rows();
    for (int i = 0; i < vr && (i + scroll_row_) < nrows_; i++) {
        int r = i + scroll_row_;
        int ry = oy + header_h() + i * row_h();
        // 交替行底色
        if (cell_color_index(r, 0) & 1)
            gfxlib::draw_rect_fill(b, ox, ry, w, row_h(), 0xFFF4F6F8u);
        // 选中高亮
        bool selected = (sel_mode_ == SelRow && r == sel_row_) ||
                        (sel_mode_ == SelCell && r == sel_row_);
        if (selected)
            gfxlib::draw_rect_fill(b, ox, ry, w, row_h(), t.selection);
        // 单元格文字
        int ccx = 0;
        for (int c = 0; c < ncols_; c++) {
            int cw = col_width(c);
            draw_text(b, ox + ccx + 4, ry + (row_h() - 7) / 2, cell(r, c), t.text);
            gfxlib::draw_line(b, ox + ccx, ry, ox + ccx, ry + row_h(), t.border);
            ccx += cw;
        }
    }
}

// ===================== 模块自检 =====================
int tableview_self_test() {
    int fail = 0;

    // --- 插入 5 行 3 列,验证 cell(2,1) ---
    {
        TableView tv;
        tv.set_bounds(0, 0, 240, 160);
        const char* headers[3] = { "Name", "Age", "City" };
        tv.set_columns(headers, 3);
        for (int r = 0; r < 5; r++) {
            char name[16]; ksprintf(name, sizeof(name), "Person%d", r);
            char age[8];   ksprintf(age, sizeof(age), "%d", 20 + r);
            const char* row[3] = { name, age, "Qingdao" };
            tv.add_row(row, 3);
        }
        if (tv.row_count() != 5) fail++;
        if (tv.col_count() != 3) fail++;
        // cell(2,1) 应为 Age=22
        if (strcmp(tv.cell(2, 1), "22") != 0) fail++;
        if (strcmp(tv.cell(0, 0), "Person0") != 0) fail++;
        // 越界访问返回空
        if (tv.cell(99, 0)[0] != 0) fail++;
    }

    // --- 设置单元格 ---
    {
        TableView tv;
        const char* h[2] = { "A", "B" };
        tv.set_columns(h, 2);
        const char* row0[2] = { "x", "y" };
        tv.add_row(row0, 2);
        tv.set_cell(0, 0, "changed");
        if (strcmp(tv.cell(0, 0), "changed") != 0) fail++;
    }

    // --- 插入/删除行 ---
    {
        TableView tv;
        const char* h[1] = { "V" };
        tv.set_columns(h, 1);
        const char* r0[1] = { "0" };
        const char* r1[1] = { "1" };
        const char* r2[1] = { "2" };
        tv.add_row(r0, 1);
        tv.add_row(r2, 1);
        tv.insert_row(1, r1, 1);   // 插到中间
        if (tv.row_count() != 3) fail++;
        if (strcmp(tv.cell(1, 0), "1") != 0) fail++;
        if (strcmp(tv.cell(2, 0), "2") != 0) fail++;
        tv.remove_row(0);
        if (tv.row_count() != 2) fail++;
        if (strcmp(tv.cell(0, 0), "1") != 0) fail++;
    }

    // --- 选择 ---
    {
        TableView tv;
        tv.set_bounds(0, 0, 240, 160);
        const char* h[2] = { "A", "B" };
        tv.set_columns(h, 2);
        for (int i = 0; i < 10; i++) {
            const char* row[2] = { "a", "b" };
            tv.add_row(row, 2);
        }
        tv.select_cell(3, 1);
        if (tv.selected_row() != 3 || tv.selected_col() != 1) fail++;
        // 键盘移动
        tv.on_key(UxKeyEvent{ UX_KEY_DOWN, 0, true });
        if (tv.selected_row() != 4) fail++;
    }

    // --- 排序 ---
    {
        TableView tv;
        const char* h[1] = { "Num" };
        tv.set_columns(h, 1);
        const char* order[5] = { "30", "10", "20", "50", "40" };
        for (int i = 0; i < 5; i++) {
            const char* row[1] = { order[i] };
            tv.add_row(row, 1);
        }
        tv.sort_by_col(0, true);
        // 字符串升序后: "10","20","30","40","50"
        if (strcmp(tv.cell(0, 0), "10") != 0) fail++;
        if (strcmp(tv.cell(4, 0), "50") != 0) fail++;
        if (tv.sorted_col() != 0) fail++;
    }

    // --- 虚拟滚动 ---
    {
        TableView tv;
        tv.set_bounds(0, 0, 240, 160);   // header 18, row 16 -> 可视 ~9 行
        const char* h[1] = { "V" };
        tv.set_columns(h, 1);
        for (int i = 0; i < 50; i++) {
            char buf[8]; ksprintf(buf, sizeof(buf), "%d", i);
            const char* row[1] = { buf };
            tv.add_row(row, 1);
        }
        int vr = tv.visible_rows();
        if (vr <= 0 || vr >= 50) fail++;   // 应为 160-18)/16 ~ 9
        tv.set_scroll_row(40);
        if (tv.scroll_row() != 40) fail++;
        // 越界滚动应被钳制
        tv.set_scroll_row(9999);
        if (tv.scroll_row() > 50 - vr) fail++;
    }

    // --- 列宽与列命中 ---
    {
        TableView tv;
        const char* h[3] = { "A", "B", "C" };
        tv.set_columns(h, 3);   // 默认各 80
        tv.set_col_width(1, 120);
        if (tv.col_width(1) != 120) fail++;
        // x=10 落在第 0 列
        if (tv.col_at_x(10) != 0) fail++;
        // x=200 落在第 2 列(80+120=200)
        if (tv.col_at_x(201) != 2) fail++;
    }

    return fail;
}

} // namespace ui
} // namespace nefu
