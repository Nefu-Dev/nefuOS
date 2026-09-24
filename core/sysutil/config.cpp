// nefuOS 系统工具扩展库 —— 配置管理模块实现
#include "config.h"
#include "../klib/klib.h"

namespace nefu {
namespace sysutil {

ConfigStore g_config;

namespace {

// 去掉字符串两端空白
void trim(const char* s, char* out, int outsz) {
    if (!out || outsz <= 0) return;
    if (!s) { out[0] = 0; return; }
    while (*s == ' ' || *s == '\t') s++;
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' ||
                     s[n-1] == '\r' || s[n-1] == '\n')) n--;
    int i = 0;
    for (; i < n && i < outsz - 1; i++) out[i] = s[i];
    out[i] = 0;
}

} // namespace

ConfigStore::ConfigStore() : version_(1) {}

void ConfigStore::clear() {
    entries_.clear();
    version_++;
}

const ConfigEntry* ConfigStore::find(const char* section, const char* key) const {
    for (int i = 0; i < entries_.size(); i++) {
        if (strcmp(entries_[i].section.c_str(), section) == 0 &&
            strcmp(entries_[i].key.c_str(), key) == 0) return &entries_[i];
    }
    return 0;
}

ConfigEntry* ConfigStore::find_mut(const char* section, const char* key) {
    for (int i = 0; i < entries_.size(); i++) {
        if (strcmp(entries_[i].section.c_str(), section) == 0 &&
            strcmp(entries_[i].key.c_str(), key) == 0) return &entries_[i];
    }
    return 0;
}

// ---------------- INI 解析 ----------------
int ConfigStore::parse_ini(const char* text) {
    if (!text) return 1;
    int errs = 0;
    String cur_sec("general");
    // 逐行扫描
    const char* p = text;
    char line[160];
    while (*p) {
        // 取一行
        int n = 0;
        while (*p && *p != '\n' && n < (int)sizeof(line) - 1) line[n++] = *p++;
        if (*p == '\n') p++;
        line[n] = 0;
        char t[160];
        trim(line, t, sizeof(t));
        if (t[0] == 0) continue;
        if (t[0] == ';' || t[0] == '#') continue;     // 注释
        if (t[0] == '[') {
            // [section]
            char* close = (char*)strchr(t, ']');
            if (!close) { errs++; continue; }
            *close = 0;
            cur_sec = t + 1;
            continue;
        }
        // key = value
        char* eq = (char*)strchr(t, '=');
        if (!eq) { errs++; continue; }
        *eq = 0;
        char k[80], v[160];
        trim(t, k, sizeof(k));
        trim(eq + 1, v, sizeof(v));
        set_str(cur_sec.c_str(), k, v);
    }
    version_++;
    return errs;
}

// ---------------- JSON 解析（扁平对象子集） ----------------
int ConfigStore::parse_json(const char* text) {
    if (!text) return 1;
    int errs = 0;
    const char* p = text;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p != '{') return errs + 1;
    p++;
    while (*p && *p != '}') {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',') p++;
        if (*p == '}') break;
        if (*p != '"') { errs++; break; }
        p++;
        char key[80]; int kn = 0;
        while (*p && *p != '"' && kn < (int)sizeof(key) - 1) key[kn++] = *p++;
        key[kn] = 0;
        if (*p != '"') { errs++; break; }
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ':') { errs++; break; }
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char val[160]; int vn = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"' && vn < (int)sizeof(val) - 1) val[vn++] = *p++;
            val[vn] = 0;
            if (*p == '"') p++;
        } else {
            while (*p && *p != ',' && *p != '}' && *p != '\n' && vn < (int)sizeof(val) - 1)
                val[vn++] = *p++;
            val[vn] = 0;
            // 去掉尾部空白
            while (vn > 0 && (val[vn-1] == ' ' || val[vn-1] == '\t')) val[--vn] = 0;
        }
        set_str("root", key, val);
    }
    version_++;
    return errs;
}

void ConfigStore::to_ini(String& out) const {
    String cur_sec;
    for (int i = 0; i < entries_.size(); i++) {
        const ConfigEntry& e = entries_[i];
        if (e.section != cur_sec) {
            cur_sec = e.section;
            out += "[";
            out += cur_sec;
            out += "]\n";
        }
        out += e.key;
        out += " = ";
        out += e.value;
        out += "\n";
    }
}

void ConfigStore::to_json(String& out) const {
    out += "{";
    bool first = true;
    for (int i = 0; i < entries_.size(); i++) {
        if (!first) out += ", ";
        first = false;
        out += "\"";
        out += entries_[i].section;
        out += ".";
        out += entries_[i].key;
        out += "\":\"";
        out += entries_[i].value;
        out += "\"";
    }
    out += "}";
}

int ConfigStore::get_int_dotted(const char* path, int dflt) const {
    if (!path) return dflt;
    // 拆 section.key
    char sec[64], key[64];
    int i = 0;
    while (path[i] && path[i] != '.' && i < 63) { sec[i] = path[i]; i++; }
    sec[i] = 0;
    if (path[i] != '.') return dflt;
    i++;
    int j = 0;
    while (path[i] && j < 63) { key[j++] = path[i++]; }
    key[j] = 0;
    return get_int(sec, key, dflt);
}

const char* ConfigStore::get_str_dotted(const char* path, const char* dflt) const {
    if (!path) return dflt;
    char sec[64], key[64];
    int i = 0;
    while (path[i] && path[i] != '.' && i < 63) { sec[i] = path[i]; i++; }
    sec[i] = 0;
    if (path[i] != '.') return dflt;
    i++;
    int j = 0;
    while (path[i] && j < 63) { key[j++] = path[i++]; }
    key[j] = 0;
    return get_str(sec, key, dflt);
}

int ConfigStore::parse_int(const char* v, int dflt) {
    if (!v) return dflt;
    while (*v == ' ') v++;
    int sign = 1;
    if (*v == '-') { sign = -1; v++; }
    if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        v += 2;
        int val = 0;
        while (*v) {
            int d = 0;
            if (*v >= '0' && *v <= '9') d = *v - '0';
            else if (*v >= 'a' && *v <= 'f') d = *v - 'a' + 10;
            else if (*v >= 'A' && *v <= 'F') d = *v - 'A' + 10;
            else break;
            val = val * 16 + d;
            v++;
        }
        return sign * val;
    }
    // 十进制：必须以数字开头，否则视为非法输入返回默认值
    if (*v < '0' || *v > '9') return dflt;
    return sign * nefu::atoi(v);
}
// ---------------- 取值 ----------------
int ConfigStore::get_int(const char* section, const char* key, int dflt) const {
    const ConfigEntry* e = find(section, key);
    if (!e) return dflt;
    return nefu::atoi(e->value.c_str());
}

const char* ConfigStore::get_str(const char* section, const char* key, const char* dflt) const {
    const ConfigEntry* e = find(section, key);
    return e ? e->value.c_str() : dflt;
}

bool ConfigStore::get_bool(const char* section, const char* key, bool dflt) const {
    const ConfigEntry* e = find(section, key);
    if (!e) return dflt;
    const char* v = e->value.c_str();
    if (v[0] == 't' || v[0] == 'T' || v[0] == '1') return true;
    if (v[0] == 'f' || v[0] == 'F' || v[0] == '0') return false;
    return dflt;
}

void ConfigStore::set_int(const char* section, const char* key, int v) {
    char buf[24];
    ksprintf(buf, sizeof(buf), "%d", v);
    set_str(section, key, buf);
}

void ConfigStore::set_str(const char* section, const char* key, const char* v) {
    if (!section || !key) return;
    ConfigEntry* e = find_mut(section, key);
    if (e) { e->value = v ? v : ""; return; }
    ConfigEntry ne;
    ne.section = section;
    ne.key = key;
    ne.value = v ? v : "";
    entries_.push(ne);
}

void ConfigStore::set_bool(const char* section, const char* key, bool v) {
    set_str(section, key, v ? "true" : "false");
}

int ConfigStore::sections(const char** out, int max) const {
    int n = 0;
    for (int i = 0; i < entries_.size() && n < max; i++) {
        bool dup = false;
        for (int j = 0; j < n; j++) {
            // out[j] 指向已有 String 的 c_str，比较之
            if (strcmp(out[j], entries_[i].section.c_str()) == 0) { dup = true; break; }
        }
        if (!dup) out[n++] = entries_[i].section.c_str();
    }
    return n;
}

int ConfigStore::keys_of(const char* section, const char** out, int max) const {
    int n = 0;
    for (int i = 0; i < entries_.size() && n < max; i++) {
        if (strcmp(entries_[i].section.c_str(), section) == 0)
            out[n++] = entries_[i].key.c_str();
    }
    return n;
}

bool ConfigStore::remove_key(const char* section, const char* key) {
    for (int i = 0; i < entries_.size(); i++) {
        if (strcmp(entries_[i].section.c_str(), section) == 0 &&
            strcmp(entries_[i].key.c_str(), key) == 0) {
            entries_.remove(i);
            version_++;
            return true;
        }
    }
    return false;
}
int ConfigStore::section_count() const {
    int n = 0;
    for (int i = 0; i < entries_.size(); i++) {
        bool dup = false;
        for (int j = 0; j < i; j++) {
            if (strcmp(entries_[j].section.c_str(), entries_[i].section.c_str()) == 0) { dup = true; break; }
        }
        if (!dup) n++;
    }
    return n;
}
bool ConfigStore::has_key(const char* section, const char* key) const {
    return find(section, key) != 0;
}
const char* ConfigStore::get_or_set(const char* section, const char* key, const char* dflt) {
    const ConfigEntry* e = find(section, key);
    if (e) return e->value.c_str();
    set_str(section, key, dflt ? dflt : "");
    return dflt ? dflt : "";
}
bool ConfigStore::rename_section(const char* section, const char* newname) {
    if (!section || !newname) return false;
    bool changed = false;
    for (int i = 0; i < entries_.size(); i++) {
        if (strcmp(entries_[i].section.c_str(), section) == 0) {
            entries_[i].section = newname;
            changed = true;
        }
    }
    if (changed) version_++;
    return changed;
}
void ConfigStore::clear_section(const char* section) {
    for (int i = entries_.size() - 1; i >= 0; i--) {
        if (strcmp(entries_[i].section.c_str(), section) == 0)
            entries_.remove(i);
    }
    version_++;
}
bool ConfigStore::parse_bool(const char* v, bool dflt) {
    if (!v) return dflt;
    while (*v == ' ') v++;
    if (v[0] == '1') return true;
    if (v[0] == '0') return false;
    if (v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y') return true;
    if (v[0] == 'f' || v[0] == 'F' || v[0] == 'n' || v[0] == 'N') return false;
    return dflt;
}

void ConfigStore::dump(String& out) const {
    for (int i = 0; i < entries_.size(); i++) {
        out += entries_[i].section;
        out += ".";
        out += entries_[i].key;
        out += "=";
        out += entries_[i].value;
        out += "\n";
    }
}
// ---------------- 校验 ----------------
int ConfigStore::validate(const ConfigRule* rules, int count) const {
    for (int i = 0; i < count; i++) {
        const ConfigRule& r = rules[i];
        const ConfigEntry* e = find(r.section, r.key);
        if (r.required && !e) return i;
        if (!e) continue;
        if (r.type == 'i') {
            int v = nefu::atoi(e->value.c_str());
            if (v < r.min_i || v > r.max_i) return i;
        }
    }
    return -1;
}

// ---------------- 自检 ----------------
int ConfigStore::self_test() {
    int fails = 0;
    ConfigStore c;

    // INI 解析
    const char* ini =
        "[general]\n"
        "name = nefuOS\n"
        "timeout = 30\n"
        "debug = true\n"
        "\n"
        "[network]\n"
        "ip = 10.0.2.15\n"
        "port = 8080\n";
    int errs = c.parse_ini(ini);
    if (errs != 0) fails++;
    if (c.count() != 5) fails++;
    if (strcmp(c.get_str("general", "name", "?"), "nefuOS") != 0) fails++;
    if (c.get_int("general", "timeout", -1) != 30) fails++;
    if (!c.get_bool("general", "debug", false)) fails++;
    if (c.get_int("network", "port", -1) != 8080) fails++;
    // 默认值
    if (c.get_int("general", "nope", 99) != 99) fails++;

    // set 覆盖
    c.set_int("network", "port", 9090);
    if (c.get_int("network", "port", -1) != 9090) fails++;
    if (c.count() != 5) fails++;   // 覆盖不增加条目
    // 新增
    c.set_bool("network", "dhcp", true);
    if (c.count() != 6) fails++;

    // 序列化往返
    String out;
    c.to_ini(out);
    // 重新解析，值应一致
    ConfigStore c2;
    c2.parse_ini(out.c_str());
    if (c2.get_int("network", "port", -1) != 9090) fails++;
    if (!c2.get_bool("network", "dhcp", false)) fails++;

    // JSON 解析
    ConfigStore c3;
    const char* js = "{\"a\": 1, \"b\": \"hello\", \"c\": false}";
    errs = c3.parse_json(js);
    if (errs != 0) fails++;
    if (c3.get_int("root", "a", -1) != 1) fails++;
    if (strcmp(c3.get_str("root", "b", "?"), "hello") != 0) fails++;
    if (c3.get_bool("root", "c", true) != false) fails++;

    // 校验
    ConfigRule rules[] = {
        {"general", "timeout", true, 'i', 1, 60},
        {"network", "port",    true, 'i', 1, 65535},
        {"general", "name",    true, 's', 0, 0},
    };
    int bad = c.validate(rules, 3);
    if (bad != -1) fails++;
    // 越界
    ConfigRule badrule[] = {
        {"general", "timeout", true, 'i', 100, 200},   // 30 越界
    };
    if (c.validate(badrule, 1) != 0) fails++;

    // 热重载版本号
    int v0 = c.version();
    c.bump();
    if (c.version() != v0 + 1) fails++;

    // 点路径取值
    c.set_int("network", "port", 9090);
    if (c.get_int_dotted("network.port", -1) != 9090) fails++;
    if (c.get_str_dotted("general.name", "?") == 0) fails++;
    // JSON 序列化往返
    String jsout;
    c.to_json(jsout);
    if (jsout.len() < 10) fails++;
    // parse_int：十进制与十六进制
    if (ConfigStore::parse_int("42", 0) != 42) fails++;
    if (ConfigStore::parse_int("0x2A", 0) != 42) fails++;
    if (ConfigStore::parse_int("nope", -7) != -7) fails++;

    // sections / keys_of / clear_section
    const char* secs[8];
    int ns = c.sections(secs, 8);
    if (ns < 2) fails++;          // general + network
    const char* keys[8];
    int nk = c.keys_of("network", keys, 8);
    if (nk < 2) fails++;          // ip + port ( +dhcp)
    c.clear_section("network");
    if (c.get_int("network", "port", -1) != -1) fails++;

    // get_bool / parse_bool
    c.set_str("flags", "debug", "true");
    c.set_str("flags", "auto", "false");
    if (!c.get_bool("flags", "debug", false)) fails++;
    if (c.get_bool("flags", "auto", true)) fails++;
    if (!ConfigStore::parse_bool("yes", false)) fails++;
    if (ConfigStore::parse_bool("no", true)) fails++;

    // rename_section
    c.set_int("oldsec", "x", 1);
    if (!c.rename_section("oldsec", "newsec")) fails++;
    if (c.get_int("newsec", "x", 0) != 1) fails++;
    if (c.get_int("oldsec", "x", 0) != 0) fails++;

    // get_or_set
    const char* gos = c.get_or_set("dyn", "key", "42");
    if (nefu::strcmp(gos, "42") != 0) fails++;
    c.set_str("dyn", "key", "99");
    if (nefu::strcmp(c.get_or_set("dyn", "key", "42"), "99") != 0) fails++;
    if (!c.has_key("dyn", "key")) fails++;
    if (c.has_key("nope", "nope")) fails++;
    if (c.section_count() < 2) fails++;
    c.set_str("tmp", "k", "v");
    if (!c.remove_key("tmp", "k")) fails++;
    if (c.has_key("tmp", "k")) fails++;
    if (c.section_count() < 1) fails++;

    return fails;
}

} // namespace sysutil
} // namespace nefu
