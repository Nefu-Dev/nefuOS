// nefuOS dblib —— CSV 实现 + 自测
#include "dblib/csv.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

namespace nefu {
namespace dbx {

// 简易状态机：逐字符扫描，支持 "..." 引号字段
std::vector<std::string> CsvTable::split_csv(const std::string& line, char delim) {
    std::vector<std::string> out;
    std::string cur;
    bool in_q = false;
    int i = 0;
    int n = (int)line.size();
    while (i < n) {
        char ch = line[i];
        if (in_q) {
            if (ch == '"') {
                // 双引号转义（"" -> "）
                if (i + 1 < n && line[i + 1] == '"') { cur += '"'; i += 2; continue; }
                in_q = false;
            } else cur += ch;
            i++;
            continue;
        }
        if (ch == '"') { in_q = true; i++; continue; }
        if (ch == delim) { out.push_back(cur); cur.clear(); i++; continue; }
        if (ch == '\r') { i++; continue; }   // 忽略 CR
        cur += ch;
        i++;
    }
    out.push_back(cur);
    return out;
}

bool CsvTable::parse(const std::string& text, char delim) {
    cells_.clear();
    rows_ = 0; cols_ = 0;
    std::string line;
    bool in_q = false;   // 跨行引号：简单按行拆，引号内逗号由 split_csv 处理
    for (size_t i = 0; i < text.size(); i++) {
        char ch = text[i];
        if (ch == '"') in_q = !in_q;
        if (ch == '\n' && !in_q) {
            std::vector<std::string> row = split_csv(line, delim);
            if (!(row.size() == 1 && row[0].empty())) {   // 跳过空行
                cells_.push_back(row);
                if ((int)row.size() > cols_) cols_ = (int)row.size();
            }
            line.clear();
            continue;
        }
        line += ch;
    }
    if (!line.empty()) {
        std::vector<std::string> row = split_csv(line, delim);
        cells_.push_back(row);
        if ((int)row.size() > cols_) cols_ = (int)row.size();
    }
    rows_ = (int)cells_.size();
    return true;
}

bool CsvTable::load(const std::string& path, char delim) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    size_t r;
    do {
        r = fread(buf, 1, sizeof(buf), f);
        text.append(buf, r);
    } while (r > 0);
    fclose(f);
    return parse(text, delim);
}

std::string CsvTable::dump(char delim) const {
    std::string out;
    for (int r = 0; r < rows_; r++) {
        std::string line;
        for (int c = 0; c < cols_; c++) {
            if (c > 0) line += delim;
            std::string v = get(r, c);
            // 含分隔符/引号/换行时加引号
            bool need = v.find(delim) != std::string::npos ||
                        v.find('"') != std::string::npos ||
                        v.find('\n') != std::string::npos;
            if (need) {
                line += '"';
                for (size_t k = 0; k < v.size(); k++) {
                    if (v[k] == '"') line += '"';
                    line += v[k];
                }
                line += '"';
            } else line += v;
        }
        out += line;
        out += '\n';
    }
    return out;
}

bool CsvTable::save(const std::string& path, char delim) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    std::string d = dump(delim);
    fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    return true;
}

std::string CsvTable::get(int r, int c) const {
    if (r < 0 || r >= rows_) return "";
    if (c < 0 || c >= (int)cells_[r].size()) return "";
    return cells_[r][c];
}

void CsvTable::set(int r, int c, const std::string& v) {
    if (r < 0 || r >= rows_) return;
    while ((int)cells_[r].size() <= c) cells_[r].push_back("");
    cells_[r][c] = v;
    if (c + 1 > cols_) cols_ = c + 1;
}

std::vector<std::string> CsvTable::header() const {
    std::vector<std::string> h;
    if (rows_ > 0)
        for (int c = 0; c < (int)cells_[0].size(); c++) h.push_back(cells_[0][c]);
    return h;
}

std::string CsvTable::get_col(const std::vector<std::string>& row,
                              const std::string& colname) const {
    std::vector<std::string> h = header();
    int idx = -1;
    for (size_t c = 0; c < h.size(); c++)
        if (h[c] == colname) { idx = (int)c; break; }
    if (idx < 0 || (int)row.size() <= idx) return "";
    return row[idx];
}

void CsvTable::add_row(const std::vector<std::string>& row) {
    cells_.push_back(row);
    rows_++;
    if ((int)row.size() > cols_) cols_ = (int)row.size();
}

double CsvTable::sum_col(const std::string& colname) const {
    std::vector<std::string> h = header();
    int idx = -1;
    for (size_t c = 0; c < h.size(); c++)
        if (h[c] == colname) { idx = (int)c; break; }
    if (idx < 0) return 0;
    double s = 0;
    for (int r = 1; r < rows_; r++)
        s += atof(get(r, idx).c_str());
    return s;
}

double CsvTable::avg_col(const std::string& colname) const {
    std::vector<std::string> h = header();
    int idx = -1;
    for (size_t c = 0; c < h.size(); c++)
        if (h[c] == colname) { idx = (int)c; break; }
    if (idx < 0 || rows_ <= 1) return 0;
    double s = 0;
    int cnt = 0;
    for (int r = 1; r < rows_; r++) {
        double v = atof(get(r, idx).c_str());
        s += v; cnt++;
    }
    return cnt == 0 ? 0 : s / cnt;
}

// ---- self test ----
int CsvTable::self_test() {
    int fails = 0;
    // 1. 基础解析
    {
        CsvTable t;
        t.parse("name,age,city\nAlice,30,Beijing\nBob,25,Shanghai\n");
        if (t.rows() != 3 || t.cols() != 3) fails++;
        if (t.get(1, 0) != "Alice") fails++;
        if (t.get(2, 2) != "Shanghai") fails++;
    }
    // 2. 引号字段
    {
        CsvTable t;
        t.parse("a,b\n\"hello, world\",2\n");
        if (t.get(1, 0) != "hello, world") fails++;
        if (t.get(1, 1) != "2") fails++;
    }
    // 3. 转义引号
    {
        CsvTable t;
        t.parse("a\n\"say \"\"hi\"\"\"\n");
        if (t.get(1, 0) != "say \"hi\"") fails++;
    }
    // 4. 往返
    {
        CsvTable t;
        t.parse("name,age\nAlice,30\n\"Bob, Jr\",25\n");
        std::string d = t.dump();
        CsvTable t2;
        t2.parse(d);
        if (t2.rows() != 3) fails++;
        if (t2.get(2, 0) != "Bob, Jr") fails++;
        if (t2.get(1, 1) != "30") fails++;
    }
    // 5. 表头与列统计
    {
        CsvTable t;
        t.parse("item,price\napple,3.5\nbanana,2\npear,4.5\n");
        std::vector<std::string> h = t.header();
        if (h.size() != 2 || h[0] != "item") fails++;
        if (std::abs(t.sum_col("price") - 10.0) > 1e-6) fails++;
        if (std::abs(t.avg_col("price") - 10.0 / 3.0) > 1e-6) fails++;
        if (t.avg_col("missing") != 0) fails++;
    }
    // 6. set 扩充列
    {
        CsvTable t;
        t.parse("a\n1\n");
        t.set(1, 1, "x");
        if (t.get(1, 1) != "x") fails++;
        if (t.cols() != 2) fails++;
    }
    // 7. csv_escape
    {
        if (csv_escape("plain") != "plain") fails++;
        if (csv_escape("a,b") != "\"a,b\"") fails++;
    }
    return fails;
}

std::string csv_escape(const std::string& field, char delim) {
    bool need = field.find(delim) != std::string::npos ||
                field.find('"') != std::string::npos ||
                field.find('\n') != std::string::npos;
    if (!need) return field;
    std::string out = "\"";
    for (size_t i = 0; i < field.size(); i++) {
        if (field[i] == '"') out += '"';
        out += field[i];
    }
    out += '"';
    return out;
}

} // namespace dbx
} // namespace nefu
