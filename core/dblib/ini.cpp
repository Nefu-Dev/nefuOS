// nefuOS dblib —— INI 实现 + 自测
#include "dblib/ini.h"
#include <cstdio>
#include <cstdlib>

namespace nefu {
namespace dbx {

std::string Ini::trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

bool Ini::parse(const std::string& text) {
    data_.clear();
    std::string section = "";
    std::string line;
    for (size_t i = 0; i <= text.size(); i++) {
        if (i == text.size() || text[i] == '\n') {
            std::string t = trim(line);
            line.clear();
            if (t.empty() || t[0] == ';' || t[0] == '#') continue;   // 空行/注释
            if (t[0] == '[' && t[t.size() - 1] == ']') {
                section = trim(t.substr(1, t.size() - 2));
                continue;
            }
            // key=value
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string k = trim(t.substr(0, eq));
            std::string v = trim(t.substr(eq + 1));
            // 去掉行尾注释（value 后的 ; #）
            size_t c1 = v.find(';'), c2 = v.find('#');
            size_t cm = std::string::npos;
            if (c1 != std::string::npos && (cm == std::string::npos || c1 < cm)) cm = c1;
            if (c2 != std::string::npos && (cm == std::string::npos || c2 < cm)) cm = c2;
            if (cm != std::string::npos) v = trim(v.substr(0, cm));
            data_[section][k] = v;
            continue;
        }
        line += text[i];
    }
    dirty_ = false;
    return true;
}

bool Ini::load(const std::string& path) {
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
    return parse(text);
}

std::string Ini::dump() const {
    std::string out;
    bool first = true;
    // 全局段在前
    std::map<std::string, std::map<std::string, std::string> >::const_iterator it;
    for (it = data_.begin(); it != data_.end(); ++it) {
        if (it->first.empty()) continue;
    }
    // 先打印全局键（section ""）
    std::map<std::string, std::string>::const_iterator g;
    for (g = data_.begin()->second.begin(); g != data_.begin()->second.end(); ++g) {
        out += g->first; out += "="; out += g->second; out += "\n";
        first = false;
    }
    // 再打印各段
    for (it = data_.begin(); it != data_.end(); ++it) {
        if (it->first.empty()) continue;
        if (!first) out += "\n";
        out += "["; out += it->first; out += "]\n";
        std::map<std::string, std::string>::const_iterator k;
        for (k = it->second.begin(); k != it->second.end(); ++k) {
            out += k->first; out += "="; out += k->second; out += "\n";
        }
        first = false;
    }
    return out;
}

bool Ini::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    std::string d = dump();
    fwrite(d.data(), 1, d.size(), f);
    fclose(f);
    return true;
}

std::string Ini::get(const std::string& section, const std::string& key,
                     const std::string& def) const {
    std::map<std::string, std::map<std::string, std::string> >::const_iterator s =
        data_.find(section);
    if (s == data_.end()) return def;
    std::map<std::string, std::string>::const_iterator k = s->second.find(key);
    if (k == s->second.end()) return def;
    return k->second;
}

int Ini::get_int(const std::string& section, const std::string& key, int def) const {
    std::string v = get(section, key, "");
    if (v.empty()) return def;
    return atoi(v.c_str());
}

double Ini::get_double(const std::string& section, const std::string& key, double def) const {
    std::string v = get(section, key, "");
    if (v.empty()) return def;
    return atof(v.c_str());
}

bool Ini::get_bool(const std::string& section, const std::string& key, bool def) const {
    std::string v = get(section, key, "");
    if (v.empty()) return def;
    return v == "true" || v == "yes" || v == "1" || v == "on";
}

void Ini::set(const std::string& section, const std::string& key, const std::string& v) {
    data_[section][key] = v;
    dirty_ = true;
}

void Ini::set_int(const std::string& section, const std::string& key, int v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    data_[section][key] = buf;
    dirty_ = true;
}

bool Ini::remove(const std::string& section, const std::string& key) {
    std::map<std::string, std::map<std::string, std::string> >::iterator s =
        data_.find(section);
    if (s == data_.end()) return false;
    std::map<std::string, std::string>::iterator k = s->second.find(key);
    if (k == s->second.end()) return false;
    s->second.erase(k);
    dirty_ = true;
    return true;
}

bool Ini::has(const std::string& section, const std::string& key) const {
    std::map<std::string, std::map<std::string, std::string> >::const_iterator s =
        data_.find(section);
    if (s == data_.end()) return false;
    return s->second.find(key) != s->second.end();
}

std::vector<std::string> Ini::sections() const {
    std::vector<std::string> out;
    std::map<std::string, std::map<std::string, std::string> >::const_iterator it;
    for (it = data_.begin(); it != data_.end(); ++it)
        if (!it->first.empty()) out.push_back(it->first);
    return out;
}

std::vector<std::string> Ini::keys(const std::string& section) const {
    std::vector<std::string> out;
    std::map<std::string, std::map<std::string, std::string> >::const_iterator s =
        data_.find(section);
    if (s == data_.end()) return out;
    std::map<std::string, std::string>::const_iterator k;
    for (k = s->second.begin(); k != s->second.end(); ++k) out.push_back(k->first);
    return out;
}

// ---- self test ----
int Ini::self_test() {
    int fails = 0;
    // 1. 解析与读取
    {
        Ini ini;
        ini.parse("[server]\nhost=localhost\nport=8080\n[user]\nname=alice\n; 注释\n# 另一注释\n");
        if (ini.get("server", "host") != "localhost") fails++;
        if (ini.get("server", "port") != "8080") fails++;
        if (ini.get("user", "name") != "alice") fails++;
        if (ini.get_int("server", "port", 0) != 8080) fails++;
        if (ini.get("server", "missing", "def") != "def") fails++;
    }
    // 2. 全局键
    {
        Ini ini;
        ini.parse("top=1\n[sec]\nx=2\n");
        if (ini.get("", "top") != "1") fails++;
    }
    // 3. 布尔与数值
    {
        Ini ini;
        ini.parse("[a]\non=true\nnum=3.14\n");
        if (!ini.get_bool("a", "on")) fails++;
        if (ini.get_double("a", "num") != 3.14) fails++;
    }
    // 4. 写入与往返
    {
        Ini ini;
        ini.set("s", "k1", "v1");
        ini.set_int("s", "k2", 42);
        ini.set("", "g", "global");
        std::string d = ini.dump();
        Ini ini2;
        ini2.parse(d);
        if (ini2.get("s", "k1") != "v1") fails++;
        if (ini2.get_int("s", "k2") != 42) fails++;
        if (ini2.get("", "g") != "global") fails++;
    }
    // 5. 删除与存在性
    {
        Ini ini;
        ini.parse("[s]\na=1\nb=2\n");
        if (!ini.has("s", "a")) fails++;
        if (!ini.remove("s", "a")) fails++;
        if (ini.has("s", "a")) fails++;
        if (ini.remove("s", "zz")) fails++;   // 删除不存在的返回 false（失败算 1）
        ini.remove("s", "a");                 // 已删，再删返回 false（不算失败）
        if (!ini.remove("s", "b")) fails++;
    }
    // 6. 段与键列表
    {
        Ini ini;
        ini.parse("[a]\nx=1\ny=2\n[b]\nz=3\n");
        std::vector<std::string> ss = ini.sections();
        if (ss.size() != 2) fails++;
        std::vector<std::string> ks = ini.keys("a");
        if (ks.size() != 2) fails++;
    }
    return fails;
}

} // namespace dbx
} // namespace nefu
