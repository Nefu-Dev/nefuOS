// nefuOS dblib —— JSON 实现 + 自测
#include "dblib/jsonstore.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace nefu {
namespace dbx {

Json Json::make_bool(bool v) { Json j; j.type_ = BOOL; j.bool_ = v; return j; }
Json Json::make_num(double v) { Json j; j.type_ = NUM; j.num_ = v; return j; }
Json Json::make_str(const std::string& v) { Json j; j.type_ = STR; j.str_ = v; return j; }
Json Json::make_arr() { Json j; j.type_ = ARR; return j; }
Json Json::make_obj() { Json j; j.type_ = OBJ; return j; }

const Json& Json::get(const std::string& k) const {
    static Json null;
    std::map<std::string, Json>::const_iterator it = obj_.find(k);
    if (it == obj_.end()) return null;
    return it->second;
}

std::vector<std::string> Json::keys() const {
    std::vector<std::string> out;
    std::map<std::string, Json>::const_iterator it;
    for (it = obj_.begin(); it != obj_.end(); ++it) out.push_back(it->first);
    return out;
}

void Json::dump_to(std::string& out) const {
    char buf[64];
    switch (type_) {
        case NUL: out += "null"; break;
        case BOOL: out += bool_ ? "true" : "false"; break;
        case NUM: {
            // 整数不带小数点
            if (num_ == (double)(long long)num_)
                snprintf(buf, sizeof(buf), "%lld", (long long)num_);
            else
                snprintf(buf, sizeof(buf), "%.6g", num_);
            out += buf;
            break;
        }
        case STR:
            out += '"';
            for (size_t i = 0; i < str_.size(); i++) {
                char c = str_[i];
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    case '\r': out += "\\r"; break;
                    default: out += c;
                }
            }
            out += '"';
            break;
        case ARR: {
            out += '[';
            for (size_t i = 0; i < arr_.size(); i++) {
                if (i > 0) out += ',';
                arr_[i].dump_to(out);
            }
            out += ']';
            break;
        }
        case OBJ: {
            out += '{';
            bool first = true;
            std::map<std::string, Json>::const_iterator it;
            for (it = obj_.begin(); it != obj_.end(); ++it) {
                if (!first) out += ',';
                first = false;
                out += '"'; out += it->first; out += "\":";
                it->second.dump_to(out);
            }
            out += '}';
            break;
        }
    }
}

std::string Json::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

void Json::Parser::skip_ws() {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        pos++;
}

bool Json::Parser::literal(const char* lit, Json& out) {
    size_t n = strlen(lit);
    if (pos + n > s.size()) return false;
    if (s.compare(pos, n, lit) != 0) return false;
    pos += n;
    out = (lit[0] == 't') ? make_bool(true) : (lit[0] == 'f') ? make_bool(false) : Json();
    if (lit[0] == 'n') out = Json();
    return true;
}

bool Json::Parser::number(Json& out) {
    size_t start = pos;
    if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) pos++;
    bool digit = false;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { pos++; digit = true; }
    if (pos < s.size() && s[pos] == '.') {
        pos++;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') { pos++; digit = true; }
    }
    if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
        pos++;
        if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') pos++;
    }
    if (!digit) return false;
    out = make_num(atof(s.substr(start, pos - start).c_str()));
    return true;
}

bool Json::Parser::parse_str(std::string& out) {
    if (pos >= s.size() || s[pos] != '"') return false;
    pos++;
    out.clear();
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '"') { pos++; return true; }
        if (c == '\\') {
            pos++;
            if (pos >= s.size()) return false;
            char e = s[pos];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                default: out += e;
            }
            pos++;
            continue;
        }
        out += c;
        pos++;
    }
    return false;
}

bool Json::Parser::parse_arr(Json& out) {
    if (pos >= s.size() || s[pos] != '[') return false;
    pos++;
    out = make_arr();
    skip_ws();
    if (pos < s.size() && s[pos] == ']') { pos++; return true; }
    while (true) {
        skip_ws();
        Json v;
        if (!parse_value(v)) return false;
        out.push(v);
        skip_ws();
        if (pos >= s.size()) return false;
        if (s[pos] == ',') { pos++; continue; }
        if (s[pos] == ']') { pos++; return true; }
        return false;
    }
}

bool Json::Parser::parse_obj(Json& out) {
    if (pos >= s.size() || s[pos] != '{') return false;
    pos++;
    out = make_obj();
    skip_ws();
    if (pos < s.size() && s[pos] == '}') { pos++; return true; }
    while (true) {
        skip_ws();
        std::string k;
        if (!parse_str(k)) return false;
        skip_ws();
        if (pos >= s.size() || s[pos] != ':') return false;
        pos++;
        skip_ws();
        Json v;
        if (!parse_value(v)) return false;
        out.set(k, v);
        skip_ws();
        if (pos >= s.size()) return false;
        if (s[pos] == ',') { pos++; continue; }
        if (s[pos] == '}') { pos++; return true; }
        return false;
    }
}

bool Json::Parser::parse_value(Json& out) {
    skip_ws();
    if (pos >= s.size()) return false;
    char c = s[pos];
    if (c == '{') return parse_obj(out);
    if (c == '[') return parse_arr(out);
    if (c == '"') {
        std::string t;
        if (!parse_str(t)) return false;
        out = make_str(t);
        return true;
    }
    if (c == 't' || c == 'f') return literal(c == 't' ? "true" : "false", out);
    if (c == 'n') return literal("null", out);
    return number(out);
}

bool Json::parse(const std::string& text, Json& out) {
    Parser p(text);
    if (!p.parse_value(out)) return false;
    p.skip_ws();
    return p.pos == text.size();
}

Json json_parse(const std::string& text) {
    Json j;
    Json::parse(text, j);
    return j;
}

// ---- self test ----
int Json::self_test() {
    int fails = 0;
    // 1. 基本类型
    {
        Json j;
        if (!Json::parse("42", j)) fails++;
        if (j.number() != 42) fails++;
        if (!Json::parse("3.5", j)) fails++;
        if (j.number() != 3.5) fails++;
        if (!Json::parse("true", j)) fails++;
        if (!j.boolean()) fails++;
        if (!Json::parse("null", j)) fails++;
        if (!j.is_null()) fails++;
        if (!Json::parse("\"hi\"", j)) fails++;
        if (j.text() != "hi") fails++;
    }
    // 2. 数组
    {
        Json j;
        if (!Json::parse("[1,2,3]", j)) fails++;
        if (j.size() != 3) fails++;
        if (j.at(0).number() != 1) fails++;
        if (j.at(2).number() != 3) fails++;
    }
    // 3. 对象嵌套
    {
        Json j;
        if (!Json::parse("{\"a\":1,\"b\":[true,false],\"c\":{\"d\":\"x\"}}", j)) fails++;
        if (j.type() != OBJ) fails++;
        if (j.get("a").number() != 1) fails++;
        if (j.get("b").size() != 2) fails++;
        if (j.get("c").get("d").text() != "x") fails++;
        if (!j.has("a") || j.has("zz")) fails++;
    }
    // 4. 转义与字符串
    {
        Json j;
        if (!Json::parse("\"a\\nb\"", j)) fails++;
        if (j.text() != "a\nb") fails++;
        if (!Json::parse("\"q\\\"q\"", j)) fails++;
        if (j.text() != "q\"q") fails++;
    }
    // 5. 序列化往返
    {
        Json j = make_obj();
        j.set("name", make_str("Alice"));
        j.set("age", make_num(30));
        Json tags = make_arr();
        tags.push(make_str("a"));
        tags.push(make_str("b"));
        j.set("tags", tags);
        std::string d = j.dump();
        Json j2;
        if (!Json::parse(d, j2)) fails++;
        if (j2.get("name").text() != "Alice") fails++;
        if (j2.get("age").number() != 30) fails++;
        if (j2.get("tags").size() != 2) fails++;
        if (j2.get("tags").at(1).text() != "b") fails++;
    }
    // 6. 空白容忍与错误
    {
        Json j;
        if (!Json::parse("  { \"a\" : 1 } ", j)) fails++;
        if (j.get("a").number() != 1) fails++;
        if (Json::parse("{bad", j)) fails++;
        if (Json::parse("[1,", j)) fails++;
    }
    // 7. 负数与小数
    {
        Json j;
        if (!Json::parse("-7", j)) fails++;
        if (j.number() != -7) fails++;
        if (!Json::parse("0.25", j)) fails++;
        if (j.number() != 0.25) fails++;
    }
    return fails;
}

} // namespace dbx
} // namespace nefu
