// nefuOS dblib —— INI 配置 ini
// 教学版：INI 配置文件的读取与写入（[section] key=value，支持注释 ; #）。
#pragma once
#include <string>
#include <vector>
#include <map>

namespace nefu {
namespace dbx {

// INI 配置：section -> (key -> value)
class Ini {
public:
    Ini() : dirty_(false) {}

    // 从文本解析
    bool parse(const std::string& text);
    // 从文件读取
    bool load(const std::string& path);
    // 写出文本
    std::string dump() const;
    // 保存到文件
    bool save(const std::string& path) const;

    // 读取（section 为空则全局段 ""）
    std::string get(const std::string& section, const std::string& key,
                    const std::string& def = "") const;
    // 数值读取
    int get_int(const std::string& section, const std::string& key, int def = 0) const;
    double get_double(const std::string& section, const std::string& key, double def = 0) const;
    bool get_bool(const std::string& section, const std::string& key, bool def = false) const;

    // 写入
    void set(const std::string& section, const std::string& key, const std::string& v);
    void set_int(const std::string& section, const std::string& key, int v);
    // 删除
    bool remove(const std::string& section, const std::string& key);
    // 是否存在
    bool has(const std::string& section, const std::string& key) const;
    // 段列表
    std::vector<std::string> sections() const;
    // 段内键列表
    std::vector<std::string> keys(const std::string& section) const;

    // ---- self test ----
    static int self_test();

private:
    std::map<std::string, std::map<std::string, std::string> > data_;
    bool dirty_;
    static std::string trim(const std::string& s);
};

} // namespace dbx
} // namespace nefu
