// nefuOS dblib —— 简易 JSON 存储 jsonstore
// 教学版：JSON 值与序列化/反序列化（对象/数组/数字/字符串/bool/null）。
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace nefu {
namespace dbx {

// JSON 值：类型判别 + 值访问
class Json {
public:
    enum Type { NUL = 0, BOOL, NUM, STR, ARR, OBJ };

    Json() : type_(NUL), num_(0), bool_(false) {}
    // 类型构造
    static Json make_bool(bool v);
    static Json make_num(double v);
    static Json make_str(const std::string& v);
    static Json make_arr();
    static Json make_obj();

    Type type() const { return type_; }
    bool is_null() const { return type_ == NUL; }

    // 值访问
    double number() const { return num_; }
    bool boolean() const { return bool_; }
    std::string text() const { return str_; }

    // 数组操作
    int size() const { return (int)arr_.size(); }
    Json& at(int i) { return arr_[i]; }
    const Json& at(int i) const { return arr_[i]; }
    void push(const Json& v) { arr_.push_back(v); type_ = ARR; }

    // 对象操作
    bool has(const std::string& k) const { return obj_.find(k) != obj_.end(); }
    Json& get(const std::string& k) { return obj_[k]; }
    const Json& get(const std::string& k) const;
    void set(const std::string& k, const Json& v) { obj_[k] = v; type_ = OBJ; }
    std::vector<std::string> keys() const;

    // 序列化
    std::string dump() const;
    // 反序列化（成功返回 true）
    static bool parse(const std::string& text, Json& out);

    // ---- self test ----
    static int self_test();

private:
    Type type_;
    double num_;
    bool bool_;
    std::string str_;
    std::vector<Json> arr_;
    std::map<std::string, Json> obj_;

    void dump_to(std::string& out) const;
    // 解析器状态
    struct Parser {
        const std::string& s;
        size_t pos;
        explicit Parser(const std::string& str) : s(str), pos(0) {}
        bool parse_value(Json& out);
        bool parse_obj(Json& out);
        bool parse_arr(Json& out);
        bool parse_str(std::string& out);
        void skip_ws();
        bool literal(const char* lit, Json& out);
        bool number(Json& out);
    };
};

// 便捷函数
Json json_parse(const std::string& text);   // 失败返回 null

} // namespace dbx
} // namespace nefu
