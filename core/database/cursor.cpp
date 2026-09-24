// nefuOS 嵌入式数据库 —— 游标实现
#include "cursor.h"

namespace nefu {
namespace database {

DbCursor::DbCursor() : t_(0), row_idx_(-1), done_(true), hit_pos_(0), using_hits_(false) {}

DbCursor::~DbCursor() { close(); }

void DbCursor::close() {
    t_ = 0;
    row_idx_ = -1;
    done_ = true;
    hit_rows_.clear();
    hit_pos_ = 0;
    using_hits_ = false;
}

// ---- 全表扫描 ----
void DbCursor::open_full(SqlTable* t) {
    close();
    t_ = t;
    using_hits_ = false;
    if (!t_ || t_->rows.size() == 0) { done_ = true; return; }
    row_idx_ = 0;
    done_ = false;
}

// ---- 索引扫描:用第一列的 B+ 树定位 ----
bool DbCursor::open_index(SqlTable* t, const String& key) {
    close();
    t_ = t;
    if (!t_ || !t_->has_index) return false;
    String idx;
    if (!t_->index.search(key, &idx)) { done_ = true; return false; }
    int row = atoi(idx.c_str());
    if (row < 0 || row >= t_->rows.size()) { done_ = true; return false; }
    // 等值索引只有一行:放入命中集,使 next() 后即结束
    hit_rows_.clear();
    hit_rows_.push(row);
    using_hits_ = true;
    hit_pos_ = 0;
    row_idx_ = row;
    done_ = false;
    return true;
}

// ---- 范围扫描:用 B+ 树取出 [lo,hi] 的所有行号 ----
void DbCursor::open_range(SqlTable* t, const String& lo, const String& hi) {
    close();
    t_ = t;
    if (!t_ || !t_->has_index) {
        // 退化成全表扫描
        open_full(t);
        return;
    }
    List<String> keys, vals;
    t_->index.range_query(lo, hi, keys, vals);
    for (int i = 0; i < vals.size(); i++) hit_rows_.push(atoi(vals[i].c_str()));
    using_hits_ = true;
    hit_pos_ = 0;
    if (hit_rows_.size() == 0) { done_ = true; return; }
    row_idx_ = hit_rows_[0];
    done_ = false;
}

void DbCursor::next() {
    if (done_) return;
    if (using_hits_) {
        hit_pos_++;
        if (hit_pos_ >= hit_rows_.size()) { done_ = true; row_idx_ = -1; return; }
        row_idx_ = hit_rows_[hit_pos_];
        return;
    }
    row_idx_++;
    if (!t_ || row_idx_ >= t_->rows.size()) { done_ = true; row_idx_ = -1; }
}

void DbCursor::prev() {
    // 索引命中集回退
    if (using_hits_) {
        if (done_) {
            // 已走到尾:回退到最后一条命中
            if (hit_rows_.size() == 0) { done_ = true; row_idx_ = -1; return; }
            done_ = false;
            hit_pos_ = hit_rows_.size() - 1;
            row_idx_ = hit_rows_[hit_pos_];
            return;
        }
        hit_pos_--;
        if (hit_pos_ < 0) { done_ = true; row_idx_ = -1; return; }
        row_idx_ = hit_rows_[hit_pos_];
        return;
    }
    // 全表回退
    if (done_) {
        if (!t_ || t_->rows.size() == 0) { done_ = true; row_idx_ = -1; return; }
        done_ = false;
        row_idx_ = t_->rows.size() - 1;
        return;
    }
    row_idx_--;
    if (row_idx_ < 0) { done_ = true; row_idx_ = -1; }
}

bool DbCursor::current_row(SqlRow& out) const {
    out.cells.clear();
    if (done_ || !t_ || row_idx_ < 0) return false;
    for (int i = 0; i < t_->rows[row_idx_].cells.size(); i++)
        out.cells.push(t_->rows[row_idx_].cells[i]);
    return true;
}

// ===================== 自检 =====================
int cursor_self_test() {
    int fail = 0;

    Database db;
    db.exec("CREATE TABLE t (id, name)");
    for (int i = 1; i <= 5; i++) {
        char buf[64]; ksprintf(buf, sizeof(buf), "INSERT INTO t VALUES (%d, 'n%d')", i, i);
        db.exec(buf);
    }
    SqlTable* tbl = db.find_table(String("t"));

    // 1) 全表扫描
    {
        DbCursor c;
        c.open_full(tbl);
        int sum = 0;
        int n = 0;
        while (!c.done()) {
            SqlRow r;
            c.current_row(r);
            sum += atoi(r.cells[0].c_str());
            n++;
            c.next();
        }
        if (n != 5) fail++;
        if (sum != 1 + 2 + 3 + 4 + 5) fail++;
    }

    // 2) 索引扫描
    {
        DbCursor c;
        if (!c.open_index(tbl, String("3"))) fail++;
        SqlRow r;
        c.current_row(r);
        if (r.cells[1] != "n3") fail++;
        c.next();
        if (!c.done()) fail++;      // 等值索引只有一行
    }

    // 3) 范围扫描
    {
        DbCursor c;
        c.open_range(tbl, String("2"), String("4"));
        int cnt = 0;
        int first = -1, last = -1;
        while (!c.done()) {
            SqlRow r; c.current_row(r);
            int v = atoi(r.cells[0].c_str());
            if (cnt == 0) first = v;
            last = v;
            cnt++;
            c.next();
        }
        if (cnt != 3) fail++;
        if (first != 2 || last != 4) fail++;
    }

    // 4) prev 回退
    {
        DbCursor c;
        c.open_range(tbl, String("1"), String("5"));
        while (!c.done()) c.next();   // 走到尾
        c.prev();                      // 回到最后一行
        SqlRow r;
        c.current_row(r);
        if (r.cells[0] != "5") fail++;
    }

    return fail;
}

} // namespace database
} // namespace nefu
