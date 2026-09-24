// nefuOS dblib —— 内存表格实现 + 自测
#include "dblib/table.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace nefu {
namespace dbx {

void Cell::parse_num() {
    // 尝试解析为 double（含负号/小数/科学计数）
    const char* p = s.c_str();
    char* end = 0;
    double v = strtod(p, &end);
    if (end != p && *end == '\0') { d = v; has_num = true; }
}

void Table::add_column(const std::string& name) {
    names_.push_back(name);
    col_count_++;
}

bool Table::insert(const std::vector<std::string>& values) {
    if ((int)values.size() != col_count_) return false;
    std::vector<Cell> row;
    for (size_t i = 0; i < values.size(); i++) row.push_back(Cell(values[i]));
    cells_.push_back(row);
    row_count_++;
    return true;
}

bool Table::remove_row(int idx) {
    if (idx < 0 || idx >= row_count_) return false;
    cells_.erase(cells_.begin() + idx);
    row_count_--;
    return true;
}

std::string Table::get(int r, int c) const {
    if (r < 0 || r >= row_count_ || c < 0 || c >= col_count_) return "";
    return cells_[r][c].s;
}

void Table::set(int r, int c, const std::string& v) {
    if (r < 0 || r >= row_count_ || c < 0 || c >= col_count_) return;
    cells_[r][c] = Cell(v);
}

int Table::column_index(const std::string& name) const {
    for (int i = 0; i < col_count_; i++)
        if (names_[i] == name) return i;
    return -1;
}

void Table::sort_by(int col, bool ascending) {
    if (col < 0 || col >= col_count_) return;
    // 稳定排序：数值优先，文本其次
    std::stable_sort(cells_.begin(), cells_.end(),
        [col, ascending](const std::vector<Cell>& a, const std::vector<Cell>& b) {
            const Cell& ca = a[col];
            const Cell& cb = b[col];
            if (ca.has_num && cb.has_num) {
                if (ascending) return ca.d < cb.d;
                return ca.d > cb.d;
            }
            if (ascending) return ca.s < cb.s;
            return ca.s > cb.s;
        });
}

Table Table::filter(int col, double minv, double maxv) const {
    Table out;
    for (int i = 0; i < col_count_; i++) out.add_column(names_[i]);
    for (int r = 0; r < row_count_; r++) {
        if (col < 0 || col >= col_count_) break;
        const Cell& c = cells_[r][col];
        if (c.has_num && c.d >= minv && c.d <= maxv) {
            std::vector<std::string> vals;
            for (int cc = 0; cc < col_count_; cc++) vals.push_back(cells_[r][cc].s);
            out.insert(vals);
        }
    }
    return out;
}

double Table::sum(int col) const {
    double s = 0;
    for (int r = 0; r < row_count_; r++) {
        const Cell& c = cells_[r][col];
        if (c.has_num) s += c.d;
    }
    return s;
}

double Table::avg(int col) const {
    double s = 0;
    int n = 0;
    for (int r = 0; r < row_count_; r++) {
        const Cell& c = cells_[r][col];
        if (c.has_num) { s += c.d; n++; }
    }
    return n == 0 ? 0 : s / n;
}

double Table::min(int col) const {
    double m = 0;
    bool first = true;
    for (int r = 0; r < row_count_; r++) {
        const Cell& c = cells_[r][col];
        if (c.has_num) { if (first || c.d < m) m = c.d; first = false; }
    }
    return first ? 0 : m;
}

double Table::max(int col) const {
    double m = 0;
    bool first = true;
    for (int r = 0; r < row_count_; r++) {
        const Cell& c = cells_[r][col];
        if (c.has_num) { if (first || c.d > m) m = c.d; first = false; }
    }
    return first ? 0 : m;
}

int Table::distinct_count(int col) const {
    std::vector<std::string> seen;
    int n = 0;
    for (int r = 0; r < row_count_; r++) {
        std::string v = cells_[r][col].s;
        bool dup = false;
        for (size_t i = 0; i < seen.size(); i++)
            if (seen[i] == v) { dup = true; break; }
        if (!dup) { seen.push_back(v); n++; }
    }
    return n;
}

// ---- self test ----
int Table::self_test() {
    int fails = 0;
    // 1. 建表与插入
    {
        Table t;
        t.add_column("name");
        t.add_column("age");
        t.add_column("score");
        std::vector<std::string> r1; r1.push_back("Alice"); r1.push_back("30"); r1.push_back("85");
        std::vector<std::string> r2; r2.push_back("Bob"); r2.push_back("25"); r2.push_back("92");
        std::vector<std::string> r3; r3.push_back("Cara"); r3.push_back("28"); r3.push_back("78");
        if (!t.insert(r1) || !t.insert(r2) || !t.insert(r3)) fails++;
        if (t.rows() != 3 || t.cols() != 3) fails++;
        if (t.get(0, 0) != "Alice") fails++;
        if (t.column_index("age") != 1) fails++;
        if (t.column_index("nope") != -1) fails++;
        // 列数不符插入失败
        std::vector<std::string> bad; bad.push_back("x");
        if (t.insert(bad)) fails++;
    }
    // 2. 统计
    {
        Table t;
        t.add_column("v");
        for (int i = 1; i <= 10; i++) {
            std::vector<std::string> r;
            char buf[16]; snprintf(buf, sizeof(buf), "%d", i * 10);
            r.push_back(buf);
            t.insert(r);
        }
        if (std::abs(t.sum(0) - 550) > 1e-9) fails++;
        if (std::abs(t.avg(0) - 55) > 1e-9) fails++;
        if (std::abs(t.min(0) - 10) > 1e-9) fails++;
        if (std::abs(t.max(0) - 100) > 1e-9) fails++;
    }
    // 3. 排序
    {
        Table t;
        t.add_column("k");
        int ks[5] = { 3, 1, 5, 2, 4 };
        for (int i = 0; i < 5; i++) {
            std::vector<std::string> r;
            char buf[16]; snprintf(buf, sizeof(buf), "%d", ks[i]);
            r.push_back(buf);
            t.insert(r);
        }
        t.sort_by(0, true);
        if (t.get(0, 0) != "1" || t.get(4, 0) != "5") fails++;
        t.sort_by(0, false);
        if (t.get(0, 0) != "5" || t.get(4, 0) != "1") fails++;
    }
    // 4. 过滤
    {
        Table t;
        t.add_column("n");
        t.add_column("v");
        for (int i = 0; i < 10; i++) {
            std::vector<std::string> r;
            char b1[8], b2[8];
            snprintf(b1, sizeof(b1), "r%d", i);
            snprintf(b2, sizeof(b2), "%d", i);
            r.push_back(b1); r.push_back(b2);
            t.insert(r);
        }
        Table f = t.filter(1, 3, 6);
        if (f.rows() != 4) fails++;
        if (f.get(0, 0) != "r3") fails++;
        if (f.get(3, 0) != "r6") fails++;
    }
    // 5. 删除
    {
        Table t;
        t.add_column("x");
        std::vector<std::string> r;
        r.push_back("a"); t.insert(r);
        r[0] = "b"; t.insert(r);
        r[0] = "c"; t.insert(r);
        if (!t.remove_row(1)) fails++;
        if (t.rows() != 2) fails++;
        if (t.get(1, 0) != "c") fails++;
        if (t.remove_row(9)) fails++;   // 越界失败
    }
    // 6. 去重
    {
        Table t;
        t.add_column("c");
        std::vector<std::string> r;
        r.push_back("a"); t.insert(r);
        r[0] = "b"; t.insert(r);
        r[0] = "a"; t.insert(r);
        r[0] = "c"; t.insert(r);
        if (t.distinct_count(0) != 3) fails++;
    }
    // 7. 文本与数字混合排序
    {
        Table t;
        t.add_column("v");
        std::vector<std::string> r;
        r.push_back("10"); t.insert(r);
        r[0] = "2"; t.insert(r);
        r[0] = "x"; t.insert(r);
        t.sort_by(0, true);
        if (t.get(0, 0) != "2") fails++;   // 数值 2 < 10
        if (t.get(2, 0) != "x") fails++;   // 文本排最后（比较运算符）
    }
    return fails;
}

} // namespace dbx
} // namespace nefu
