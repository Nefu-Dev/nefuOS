// nefuOS dblib —— 内存表格 table
// 教学版：内存表（列名 + 行），支持插入/删除/排序/过滤/统计。
#pragma once
#include <string>
#include <vector>

namespace nefu {
namespace dbx {

// 单元格类型
struct Cell {
    std::string s;   // 字符串值
    double d;        // 数值（解析失败为 NaN）
    bool has_num;
    Cell() : d(0), has_num(false) {}
    Cell(const std::string& v) : s(v), d(0), has_num(false) { parse_num(); }
    void parse_num();
};

// 内存表：列定义 + 行
class Table {
public:
    Table() : col_count_(0), row_count_(0) {}

    // 定义列
    void add_column(const std::string& name);
    // 插入一行（按列顺序）
    bool insert(const std::vector<std::string>& values);
    // 删除行（按索引）
    bool remove_row(int idx);
    // 单元格
    std::string get(int r, int c) const;
    void set(int r, int c, const std::string& v);
    // 按列名找列号（-1 无）
    int column_index(const std::string& name) const;

    // 排序（按列；ascending）
    void sort_by(int col, bool ascending);
    // 过滤：保留满足谓词的行（复制出新表）
    Table filter(int col, double minv, double maxv) const;
    // 统计
    double sum(int col) const;
    double avg(int col) const;
    double min(int col) const;
    double max(int col) const;
    // 去重行数（按第 col 列）
    int distinct_count(int col) const;

    int cols() const { return col_count_; }
    int rows() const { return row_count_; }
    std::vector<std::string> column_names() const { return names_; }

    // ---- self test ----
    static int self_test();

private:
    std::vector<std::string> names_;
    std::vector<std::vector<Cell> > cells_;
    int col_count_, row_count_;
};

} // namespace dbx
} // namespace nefu
