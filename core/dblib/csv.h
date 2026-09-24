// nefuOS dblib —— CSV 读写器 csv
// 教学版：CSV（逗号分隔值）解析与写出，支持引号字段、转义、换行。
// 用 std::string / std::vector / do-while / switch，中文注释。
#pragma once
#include <string>
#include <vector>

namespace nefu {
namespace dbx {

// CSV 表格：行 = vector<string>
class CsvTable {
public:
    CsvTable() : rows_(0), cols_(0) {}

    // 解析 CSV 文本
    bool parse(const std::string& text, char delim = ',');
    // 解析文件内容（按字节读入再 parse）
    bool load(const std::string& path, char delim = ',');

    // 写出为 CSV 文本
    std::string dump(char delim = ',') const;
    // 写出到文件
    bool save(const std::string& path, char delim = ',') const;

    // 行/列
    int rows() const { return rows_; }
    int cols() const { return cols_; }
    // 单元格访问（越界返回空串）
    std::string get(int r, int c) const;
    void set(int r, int c, const std::string& v);
    // 表头行
    std::vector<std::string> header() const;
    // 按列名取行值（找不到返回空串）
    std::string get_col(const std::vector<std::string>& row,
                        const std::string& colname) const;

    // 追加行
    void add_row(const std::vector<std::string>& row);
    // 统计：某列数值求和（无效数字计 0）
    double sum_col(const std::string& colname) const;
    // 统计：某列平均值
    double avg_col(const std::string& colname) const;

    // ---- self test ----
    static int self_test();

private:
    std::vector<std::vector<std::string> > cells_;
    int rows_, cols_;
    static std::vector<std::string> split_csv(const std::string& line, char delim);
};

// 单字段 CSV 转义（用于手工构造）
std::string csv_escape(const std::string& field, char delim = ',');

} // namespace dbx
} // namespace nefu
