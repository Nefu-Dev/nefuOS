// nefuOS 数据序列化与编解码库 —— 文本格式模块实现
// 见 textfmt.h 的设计说明。这里逐个实现 8 种文本格式的解析/生成与自测。
#include "textfmt.h"
#include <stdarg.h>
#include <stdint.h>

namespace nefu {
namespace serialize {

// ============================================================================
// 内部小工具（匿名命名空间）
// ============================================================================
namespace {

// ---- 字符分类（手写，避免依赖 ctype，且 freestanding 可用）----
inline bool is_digit(char c)  { return c >= '0' && c <= '9'; }
inline bool is_xdigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }
inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
inline int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

// ---- 字符串构建助手 ----
// 追加一个 int64（ksprintf 不支持 %lld，这里手写）
void append_i64(String& out, long long v) {
    char tmp[24];
    int n = 0;
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(-(v + 1)) + 1ull : (unsigned long long)v;
    if (u == 0) { out += '0'; return; }
    while (u > 0) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg) out += '-';
    while (n > 0) out += tmp[--n];
}

// 追加 double（保留最多 6 位小数，去掉多余的 0）
void append_double(String& out, double v) {
    // 不依赖 <math.h> 的 host-only 部分；用简单整数化近似：先写整数部分
    if (v != v) { out += "NaN"; return; }                 // NaN
    // 处理 inf
    // （nefuOS 无 libm 时 Infinity 检测靠位模式下面用分支）
    long long ip = (long long)v;
    append_i64(out, ip);
    double frac = v - (double)ip;
    if (frac < 0) frac = -frac;
    if (frac > 1e-9) {
        out += '.';
        // 取 6 位小数
        for (int i = 0; i < 6; i++) {
            frac *= 10.0;
            int d = (int)frac;
            if (d < 0) d = 0;
            if (d > 9) d = 9;
            out += (char)('0' + d);
            frac -= (double)d;
        }
    }
}

// 跳过空白（含 \r \n）
inline const char* skip_ws(const char* p) {
    while (*p && is_space(*p)) p++;
    return p;
}

// 跳过不可见空白但不跨行（用于 INI/ENV 的 '=' 两侧）
inline const char* skip_flat_ws(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// 报告错误：把 err 指针指向静态常量串
inline void report(const char** err, const char* msg) {
    if (err) *err = msg;
}

} // namespace

// ============================================================================
// VValue 通用动态值实现
// ============================================================================
int VValue::size() const {
    if (type == VType::V_ARRAY) return arr.size();
    if (type == VType::V_OBJECT) return obj.size();
    return 0;
}

const VValue* VValue::get(const char* key) const {
    if (type != VType::V_OBJECT) return 0;
    for (int i = 0; i < obj.size(); i++)
        if (strcmp(obj[i].key.c_str(), key) == 0) return obj[i].val;
    return 0;
}
VValue* VValue::get(const char* key) {
    if (type != VType::V_OBJECT) return 0;
    for (int i = 0; i < obj.size(); i++)
        if (strcmp(obj[i].key.c_str(), key) == 0) return obj[i].val;
    return 0;
}
bool VValue::has(const char* key) const { return get(key) != 0; }

void VValue::set(const char* key, const VValue& v) {
    if (type != VType::V_OBJECT) { type = VType::V_OBJECT; }
    if (VValue* exist = get(key)) {
        // 替换旧值（深拷贝新值）
        free_value(exist);
        *exist = v;
        return;
    }
    VMember m;
    m.key = key;
    m.val = VValue::clone(v);
    obj.push(m);
}

void VValue::push(const VValue& v) {
    if (type != VType::V_ARRAY) { type = VType::V_ARRAY; }
    arr.push(VValue::clone(v));
}

VValue* VValue::clone(const VValue& o) {
    VValue* n = new VValue();
    n->type = o.type;
    n->b = o.b; n->i = o.i; n->d = o.d; n->s = o.s;
    if (o.type == VType::V_ARRAY) {
        for (int k = 0; k < o.arr.size(); k++) n->arr.push(VValue::clone(*o.arr[k]));
    } else if (o.type == VType::V_OBJECT) {
        for (int k = 0; k < o.obj.size(); k++) {
            VMember m; m.key = o.obj[k].key; m.val = VValue::clone(*o.obj[k].val);
            n->obj.push(m);
        }
    }
    return n;
}

void free_value(VValue* v) {
    if (!v) return;
    for (int i = 0; i < v->arr.size(); i++) free_value(v->arr[i]);
    for (int i = 0; i < v->obj.size(); i++) free_value(v->obj[i].val);
    delete v;
}

// ============================================================================
// 1. CSV
// ============================================================================
int CsvDoc::cols_count() const {
    int m = 0;
    for (int r = 0; r < rows.size(); r++)
        if (rows[r].size() > m) m = rows[r].size();
    return m;
}
const char* CsvDoc::cell(int r, int c) const {
    if (r < 0 || r >= rows.size()) return "";
    const List<String>& row = rows[r];
    if (c < 0 || c >= row.size()) return "";
    return row[c].c_str();
}

// CSV 解析状态机：逐字符读取，处理引号字段（含内部逗号、"" 转义、换行）。
bool csv_parse(const char* text, CsvDoc& out, char delim, const char** err) {
    out.rows.clear();
    out.delim = delim;
    if (!text) { report(err, "csv: null input"); return false; }

    List<String> row;          // 当前记录（行）的字段
    String field;              // 当前正在累积的字段
    bool in_quotes = false;    // 当前字段是否处于引号内
    bool field_started = false; // 已经进入当前字段（区分空字段与间隙）

    // 提交当前字段并开启下一个字段
    auto end_field = [&]() {
        row.push(field);
        field = String();
        field_started = false;
    };
    // 提交当前记录到文档并清空 row
    auto end_record = [&]() {
        end_field();
        out.rows.push(row);
        row.clear();
    };

    for (const char* p = text; ; p++) {
        char c = *p;
        if (c == 0) break;

        if (in_quotes) {
            if (c == '"') {
                if (p[1] == '"') { field += '"'; p++; }   // "" -> "
                else in_quotes = false;
            } else {
                field += c;                               // 引号内一切原样（含换行）
            }
            continue;
        }

        if (c == '"' && !field_started) {
            // 字段以引号开头：进入引号模式（允许字段内含逗号）
            in_quotes = true;
            field_started = true;
            continue;
        }
        if (c == delim) {
            end_field();
            continue;
        }
        if (c == '\r') {
            if (p[1] == '\n') p++;   // CRLF：吞掉配对的 \n
            end_record();
            continue;
        }
        if (c == '\n') {
            end_record();
            continue;
        }
        field += c;
        field_started = true;
    }

    // EOF：若当前记录非空（有字段内容或至少起过头），补提交最后一行
    if (row.size() > 0 || field.len() > 0 || field_started) {
        end_record();
    }

    (void)err;
    return true;
}

// 字段是否需要引号包裹：含分隔符、引号、换行时必须加引号
static bool csv_needs_quote(const String& f, char delim) {
    for (int i = 0; i < f.len(); i++) {
        char c = f[i];
        if (c == delim || c == '"' || c == '\n' || c == '\r') return true;
    }
    return false;
}

void csv_write(const CsvDoc& doc, String& out) {
    for (int r = 0; r < doc.rows.size(); r++) {
        const List<String>& row = doc.rows[r];
        for (int c = 0; c < row.size(); c++) {
            const String& f = row[c];
            if (csv_needs_quote(f, doc.delim)) {
                out += '"';
                for (int i = 0; i < f.len(); i++) {
                    if (f[i] == '"') out += '"';   // 双引号转义
                    out += f[i];
                }
                out += '"';
            } else {
                out += f;
            }
            if (c + 1 < row.size()) out += doc.delim;
        }
        out += '\r';
        out += '\n';
    }
}

int csv_self_test() {
    int fail = 0;
    // 用例 1：简单表格
    {
        const char* txt = "a,b,c\n1,2,3\nx,y,z\n";
        CsvDoc d;
        if (!csv_parse(txt, d) || d.rows_count() != 3) fail++;
        else if (strcmp(d.cell(0, 0), "a") != 0 || strcmp(d.cell(2, 2), "z") != 0) fail++;
    }
    // 用例 2：引号字段（内含逗号）
    {
        const char* txt = "name,note\n\"Doe, John\",\"has \"\"quotes\"\"\"\n";
        CsvDoc d;
        if (!csv_parse(txt, d)) fail++;
        else {
            if (d.rows_count() != 2) fail++;
            if (strcmp(d.cell(1, 0), "Doe, John") != 0) fail++;
            if (strcmp(d.cell(1, 1), "has \"quotes\"") != 0) fail++;
        }
    }
    // 用例 3：多行引号字段（字段内含换行）
    {
        const char* txt = "k,note\n1,\"line1\nline2\"\n2,x\n";
        CsvDoc d;
        if (!csv_parse(txt, d)) fail++;
        else {
            // 第一行 k,note；第二字段跨行；总共 3 行
            if (d.rows_count() != 3) fail++;
            if (strcmp(d.cell(1, 1), "line1\nline2") != 0) fail++;
            if (strcmp(d.cell(2, 0), "2") != 0) fail++;
        }
    }
    // 用例 4：round-trip（生成后再解析，字段值应一致）
    {
        CsvDoc in;
        List<String> r1; r1.push("name"); r1.push("city");
        List<String> r2; r2.push("Doe, J."); r2.push("New\nYork");
        in.rows.push(r1); in.rows.push(r2);
        String out;
        csv_write(in, out);
        CsvDoc back;
        if (!csv_parse(out.c_str(), back)) fail++;
        else {
            if (strcmp(back.cell(1, 0), "Doe, J.") != 0) fail++;
            if (strcmp(back.cell(1, 1), "New\nYork") != 0) fail++;
        }
    }
    // 用例 5：制表符分隔
    {
        const char* txt = "a\tb\tc\n1\t2\t3\n";
        CsvDoc d;
        if (!csv_parse(txt, d, '\t')) fail++;
        else if (strcmp(d.cell(1, 2), "3") != 0) fail++;
    }
    return fail;
}

// ============================================================================
// 2. INI
// ============================================================================
IniSection* IniDoc::find_section(const char* name) {
    for (int i = 0; i < sections.size(); i++)
        if (strcmp(sections[i].name.c_str(), name) == 0) return &sections[i];
    return 0;
}
const char* IniDoc::get(const char* section, const char* key, const char* def) {
    IniSection* s = find_section(section);
    if (!s) return def;
    for (int i = 0; i < s->entries.size(); i++)
        if (strcmp(s->entries[i].key.c_str(), key) == 0) return s->entries[i].value.c_str();
    return def;
}
void IniDoc::set(const char* section, const char* key, const char* value) {
    IniSection* s = find_section(section);
    if (!s) {
        IniSection ns; ns.name = section;
        sections.push(ns);
        s = &sections[sections.size() - 1];
    }
    for (int i = 0; i < s->entries.size(); i++) {
        if (strcmp(s->entries[i].key.c_str(), key) == 0) {
            s->entries[i].value = value;
            return;
        }
    }
    IniEntry e; e.key = key; e.value = value;
    s->entries.push(e);
}

bool ini_parse(const char* text, IniDoc& out, const char** err) {
    out.sections.clear();
    if (!text) { report(err, "ini: null input"); return false; }

    IniSection global;   // 全局区（[section] 之前）
    out.sections.push(global);
    IniSection* cur = &out.sections[0];

    // 按行切分
    List<String> lines;
    String line;
    for (const char* p = text; ; p++) {
        char c = *p;
        if (c == '\n' || c == 0) {
            lines.push(line);
            line = String();
            if (c == 0) break;
            continue;
        }
        if (c != '\r') line += c;
    }

    for (int li = 0; li < lines.size(); li++) {
        const String& L = lines[li];
        const char* p = skip_flat_ws(L.c_str());
        if (*p == 0) continue;                 // 空行
        if (*p == ';' || *p == '#') continue;  // 整行注释

        if (*p == '[') {
            // [section]
            const char* end = strchr(p, ']');
            if (!end) { report(err, "ini: unterminated [section]"); return false; }
            String name(p + 1, (int)(end - (p + 1)));
            // 去掉首尾空白
            // 查找或新建 section
            IniSection* s = out.find_section(name.c_str());
            if (!s) { IniSection ns; ns.name = name; out.sections.push(ns); s = &out.sections[out.sections.size() - 1]; }
            cur = s;
            continue;
        }

        // key = value  /  key : value
        // 找 '=' 或 ':'（第一个出现的）
        const char* eq = 0;
        for (const char* q = p; *q; q++) {
            if (*q == '=' || *q == ':') { eq = q; break; }
        }
        if (!eq) continue;   // 无法解析的行，跳过
        String key(p, (int)(eq - p));
        // 去掉 key 尾部空白
        while (key.len() > 0 && (key[key.len() - 1] == ' ' || key[key.len() - 1] == '\t'))
            key = key.substr(0, key.len() - 1);

        const char* vstart = skip_flat_ws(eq + 1);
        String val;
        String comment;
        for (const char* q = vstart; *q; q++) {
            // 内联注释：未加引号的 '#' 或 ';'
            if ((*q == '#' || *q == ';') && val.len() > 0) {
                // 要求注释前是空白（简单规则）
                char prev = val[val.len() - 1];
                if (prev == ' ' || prev == '\t') {
                    comment = q + 1;
                    // 去掉注释前空格
                    while (val.len() > 0 && (val[val.len() - 1] == ' ' || val[val.len() - 1] == '\t'))
                        val = val.substr(0, val.len() - 1);
                    break;
                }
            }
            val += *q;
        }
        // 值去除首尾空白
        while (val.len() > 0 && (val[0] == ' ' || val[0] == '\t'))
            val = val.substr(1, val.len() - 1);
        while (val.len() > 0 && (val[val.len() - 1] == ' ' || val[val.len() - 1] == '\t'))
            val = val.substr(0, val.len() - 1);
        // 去值两端引号
        if (val.len() >= 2 && ((val[0] == '"' && val[val.len() - 1] == '"') ||
                               (val[0] == '\'' && val[val.len() - 1] == '\''))) {
            val = val.substr(1, val.len() - 2);
        }

        IniEntry e; e.key = key; e.value = val; e.comment = comment;
        cur->entries.push(e);
    }
    return true;
}

void ini_write(const IniDoc& doc, String& out) {
    for (int s = 0; s < doc.sections.size(); s++) {
        const IniSection& sec = doc.sections[s];
        if (s == 0 && sec.name.len() == 0) {
            // 全局区：直接写 key=value
        } else {
            out += '['; out += sec.name; out += "]\n";
        }
        for (int i = 0; i < sec.entries.size(); i++) {
            out += sec.entries[i].key;
            out += " = ";
            out += sec.entries[i].value;
            out += '\n';
        }
        out += '\n';
    }
}

int ini_self_test() {
    int fail = 0;
    const char* txt =
        "; 顶层注释\n"
        "global_key = 123\n"
        "[owner]\n"
        "name = John Doe   ; 内联注释\n"
        "organization = Acme\n"
        "[database]\n"
        "port = 5432\n"
        "host = \"127.0.0.1\"\n";
    IniDoc d;
    if (!ini_parse(txt, d)) fail++;
    else {
        if (strcmp(d.get("", "global_key"), "123") != 0) fail++;
        if (strcmp(d.get("owner", "name"), "John Doe") != 0) fail++;
        if (strcmp(d.get("owner", "organization"), "Acme") != 0) fail++;
        if (strcmp(d.get("database", "port"), "5432") != 0) fail++;
        if (strcmp(d.get("database", "host"), "127.0.0.1") != 0) fail++;
        if (strcmp(d.get("nope", "k", "def"), "def") != 0) fail++;
    }
    // round-trip：set 后写出再解析
    {
        IniDoc d2;
        d2.set("user", "id", "42");
        d2.set("user", "role", "admin");
        String out;
        ini_write(d2, out);
        IniDoc back;
        if (!ini_parse(out.c_str(), back)) fail++;
        else if (strcmp(back.get("user", "id"), "42") != 0) fail++;
        else if (strcmp(back.get("user", "role"), "admin") != 0) fail++;
    }
    return fail;
}

// ============================================================================
// 3. XML —— DOM 解析与生成
// ============================================================================
XmlNode* XmlNode::child(const char* name) const {
    for (int i = 0; i < children.size(); i++)
        if (strcmp(children[i]->name.c_str(), name) == 0) return children[i];
    return 0;
}
XmlNode* XmlNode::add_child(const char* name) {
    XmlNode* n = new XmlNode();
    n->name = name;
    children.push(n);
    return n;
}
const char* XmlNode::attr(const char* name, const char* def) const {
    for (int i = 0; i < attrs.size(); i++)
        if (strcmp(attrs[i].name.c_str(), name) == 0) return attrs[i].value.c_str();
    return def;
}
void XmlNode::set_attr(const char* name, const char* value) {
    for (int i = 0; i < attrs.size(); i++) {
        if (strcmp(attrs[i].name.c_str(), name) == 0) { attrs[i].value = value; return; }
    }
    XmlAttr a; a.name = name; a.value = value;
    attrs.push(a);
}

void xml_free(XmlDoc& doc) {
    // 递归释放
    struct F {
        static void free_node(XmlNode* n) {
            if (!n) return;
            for (int i = 0; i < n->children.size(); i++) free_node(n->children[i]);
            delete n;
        }
    };
    F::free_node(doc.root);
    doc.root = 0;
}

// XML 实体解码：&lt; &gt; &quot; &apos; &amp; &#NN;
static String xml_unescape(const String& in) {
    String out;
    for (int i = 0; i < in.len(); i++) {
        char c = in[i];
        if (c != '&') { out += c; continue; }
        const char* p = in.c_str() + i;
        if (strncmp(p, "&lt;", 4) == 0) { out += '<'; i += 3; }
        else if (strncmp(p, "&gt;", 4) == 0) { out += '>'; i += 3; }
        else if (strncmp(p, "&quot;", 6) == 0) { out += '"'; i += 5; }
        else if (strncmp(p, "&apos;", 6) == 0) { out += '\''; i += 5; }
        else if (strncmp(p, "&amp;", 5) == 0) { out += '&'; i += 4; }
        else if (p[1] == '#') {
            // &#NN; 十进制 / &#xNN; 十六进制
            int v = 0; int j = 2;
            if (p[2] == 'x' || p[2] == 'X') {
                j = 3;
                while (is_xdigit(p[j])) { v = v * 16 + hex_val(p[j]); j++; }
            } else {
                while (is_digit(p[j])) { v = v * 10 + (p[j] - '0'); j++; }
            }
            if (p[j] == ';') {
                // 只保留 ASCII 范围；>127 的按 UTF-8 简化为 '?'
                if (v < 128) out += (char)v;
                else out += '?';
                i = j;
            } else out += '&';
        } else out += '&';
    }
    return out;
}

static String xml_escape(const String& in) {
    String out;
    for (int i = 0; i < in.len(); i++) {
        char c = in[i];
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        case '&': out += "&amp;"; break;
        default: out += c;
        }
    }
    return out;
}

// XML 解析：递归下降。p 是指针引用，随解析推进。
struct XmlParser {
    const char* p;
    const char** err;

    const char* skip() {
        while (*p) {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; continue; }
            break;
        }
        return p;
    }

    // 跳过 <!-- ... -->
    bool skip_comment() {
        if (strncmp(p, "<!--", 4) != 0) return false;
        p += 4;
        const char* e = strstr(p, "-->");
        if (!e) { if (err) *err = "xml: unterminated comment"; return true; }
        p = e + 3;
        return true;
    }
    // 跳过 <?pi?>  <!DOCTYPE...>
    bool skip_decl() {
        if (*p != '<') return false;
        if (p[1] == '?') {
            const char* e = strstr(p, "?>");
            if (!e) { if (err) *err = "xml: unterminated processing instruction"; return true; }
            p = e + 2;
            return true;
        }
        if (strncmp(p, "<!", 2) == 0 && strncmp(p, "<!--", 4) != 0) {
            const char* e = strchr(p, '>');
            if (!e) { if (err) *err = "xml: unterminated <!> decl"; return true; }
            p = e + 1;
            return true;
        }
        return false;
    }

    XmlNode* parse_element();

    // 解析标签名（可能带前缀 ns:local）
    String parse_name() {
        String n;
        while (*p && (is_alnum(*p) || *p == ':' || *p == '-' || *p == '.' || *p == '_')) {
            n += *p; p++;
        }
        return n;
    }

    // 解析属性： attr="value"
    void parse_attrs(XmlNode* node) {
        for (;;) {
            skip();
            if (*p == '>' || *p == '/' || *p == 0) return;
            String name = parse_name();
            skip();
            if (*p == '=') {
                p++; skip();
                char q = *p;
                if (q == '"' || q == '\'') {
                    p++;
                    String val;
                    while (*p && *p != q) { val += *p; p++; }
                    if (*p == q) p++;
                    XmlAttr a; a.name = name; a.value = xml_unescape(val);
                    node->attrs.push(a);
                }
            }
        }
    }
};

XmlNode* XmlParser::parse_element() {
    // 入口：p 指向 '<'
    if (*p != '<') { if (err) *err = "xml: expected '<'"; return 0; }
    p++;
    if (*p == '/') return 0;   // 结束标签不该在此
    if (strncmp(p, "![CDATA[", 8) == 0) {
        // CDATA 作为兄弟文本由上层处理；这里理论上不会进
        p += 8; return 0;
    }
    String name = parse_name();
    XmlNode* node = new XmlNode();
    node->name = name;
    // 命名空间前缀
    const char* colon = strchr(name.c_str(), ':');
    if (colon) node->ns_prefix = String(name.c_str(), (int)(colon - name.c_str()));

    parse_attrs(node);
    skip();
    bool self_close = false;
    if (*p == '/') { self_close = true; p++; }
    if (*p == '>') p++;
    if (self_close) return node;

    // 内容：混合文本 + 子元素
    String text;
    for (;;) {
        if (*p == 0) { if (err) *err = "xml: unexpected EOF"; delete node; return 0; }
        if (strncmp(p, "<!--", 4) == 0) { skip_comment(); continue; }
        if (strncmp(p, "<![CDATA[", 9) == 0) {
            p += 9;
            const char* e = strstr(p, "]]>");
            if (e) { text += String(p, (int)(e - p)); p = e + 3; }
            else { text += p; p += strlen(p); }
            continue;
        }
        if (p[0] == '<' && p[1] == '?') { skip_decl(); continue; }
        if (p[0] == '<' && p[1] == '!') { skip_decl(); continue; }
        if (*p == '<') {
            // 结束标签？
            if (p[1] == '/') {
                p += 2;
                parse_name();       // 跳过同名结束标签名（不校验）
                skip();
                if (*p == '>') p++;
                node->text = xml_unescape(text);
                return node;
            }
            // 文本累积完后，遇到子元素
            if (text.len() > 0) {
                // 合并相邻文本：把文本塞进本节点 text（保留）
                String dec = xml_unescape(text);
                node->text += dec;
                text = String();
            }
            XmlNode* child = parse_element();
            if (!child) { delete node; return 0; }
            node->children.push(child);
            continue;
        }
        text += *p;
        p++;
    }
}

bool xml_parse(const char* text, XmlDoc& out, const char** err) {
    xml_free(out);
    out.root = 0;
    if (!text) { if (err) *err = "xml: null input"; return false; }
    XmlParser ps; ps.p = text; ps.err = err;
    // 跳过开头的声明/注释/空白
    for (;;) {
        ps.skip();
        if (strncmp(ps.p, "<!--", 4) == 0) { ps.skip_comment(); continue; }
        if (ps.p[0] == '<' && ps.p[1] == '?') { ps.skip_decl(); continue; }
        if (ps.p[0] == '<' && ps.p[1] == '!') { ps.skip_decl(); continue; }
        break;
    }
    out.root = ps.parse_element();
    if (!out.root) return false;
    // 取版本
    return true;
}

static void xml_write_node(const XmlNode* node, String& out, int indent, bool pretty) {
    for (int i = 0; i < indent; i++) out += "  ";
    out += '<'; out += node->name;
    for (int i = 0; i < node->attrs.size(); i++) {
        out += ' '; out += node->attrs[i].name;
        out += "=\""; out += xml_escape(node->attrs[i].value); out += '"';
    }
    if (node->children.size() == 0 && node->text.len() == 0) {
        out += "/>\n";
        return;
    }
    out += '>';
    if (node->text.len() > 0 && node->children.size() == 0) {
        out += xml_escape(node->text);
        out += '<'; out += '/'; out += node->name; out += ">\n";
        return;
    }
    if (pretty) out += '\n';
    if (node->text.len() > 0) {
        for (int i = 0; i < indent + 1; i++) out += "  ";
        out += xml_escape(node->text);
        if (pretty) out += '\n';
    }
    for (int i = 0; i < node->children.size(); i++)
        xml_write_node(node->children[i], out, indent + 1, pretty);
    if (pretty) for (int i = 0; i < indent; i++) out += "  ";
    out += '<'; out += '/'; out += node->name; out += ">\n";
}

void xml_write(const XmlDoc& doc, String& out, bool pretty) {
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    if (doc.root) xml_write_node(doc.root, out, 0, pretty);
}

int xml_self_test() {
    int fail = 0;
    const char* txt =
        "<?xml version=\"1.0\"?>\n"
        "<root xmlns:me=\"http://x\">\n"
        "  <!-- a comment -->\n"
        "  <person me:id=\"7\" name=\"John\">Hello &lt;world&gt;</person>\n"
        "  <items>\n"
        "    <item>one</item>\n"
        "    <![CDATA[ raw < > text ]]>\n"
        "  </items>\n"
        "  <empty/>\n"
        "</root>\n";
    XmlDoc d;
    const char* err = 0;
    if (!xml_parse(txt, d, &err)) { fail++; }
    else {
        if (!d.root) fail++;
        else {
            if (strcmp(d.root->name.c_str(), "root") != 0) fail++;
            XmlNode* person = d.root->child("person");
            if (!person) fail++;
            else {
                if (strcmp(person->attr("name"), "John") != 0) fail++;
                if (strcmp(person->attr("me:id"), "7") != 0) fail++;
                if (strcmp(person->text.c_str(), "Hello <world>") != 0) fail++;
            }
            XmlNode* items = d.root->child("items");
            if (!items) fail++;
            else {
                XmlNode* it = items->child("item");
                if (!it || strcmp(it->text.c_str(), "one") != 0) fail++;
            }
            if (!d.root->child("empty")) fail++;
        }
        // round-trip：写出再解析
        String out;
        xml_write(d, out, true);
        XmlDoc d2;
        if (!xml_parse(out.c_str(), d2)) fail++;
        else {
            XmlNode* p = d2.root ? d2.root->child("person") : 0;
            if (!p || strcmp(p->attr("name"), "John") != 0) fail++;
        }
        xml_free(d2);
    }
    xml_free(d);
    (void)err;
    return fail;
}

// ============================================================================
// 4. TOML —— 最小子集解析/生成
// ============================================================================
namespace {

// 跳过 TOML 行内空白与注释（不跨行）
const char* toml_skip_inline(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// 读一个 TOML 基本字符串（"..."）或字面字符串（'...'），返回新分配的 VValue 字符串。
// p 指向开头引号；*endp 推进到结束引号之后。
VValue* toml_read_string(const char* p, const char** endp, bool* had_error) {
    char q = *p;
    p++;
    String out;
    if (q == '\'') {
        // 字面字符串：不做转义
        while (*p && *p != '\'') { out += *p; p++; }
        if (*p == '\'') p++;
        else *had_error = true;
    } else {
        // 基本字符串：处理转义
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case 'u': {
                    // \uXXXX：取 4 位 hex（简化：仅当 <128 保留字符）
                    int cp = 0; int k = 0;
                    for (k = 0; k < 4 && is_xdigit(p[1]); k++, p++) cp = cp * 16 + hex_val(p[1]);
                    (void)k;
                    if (cp < 128) out += (char)cp; else out += '?';
                    break;
                }
                default: out += *p;
                }
                p++;
            } else {
                out += *p++;
            }
        }
        if (*p == '"') p++;
        else *had_error = true;
    }
    if (endp) *endp = p;
    return new VValue(out);
}

// 读一个 TOML 值：字符串/数字/布尔/数组/内联表
VValue* toml_read_value(const char* p, const char** endp, const char** err);

// 解析 [a.b.c] 或 [[a.b.c]] 表头，返回按 '.' 切好的名字列表
void toml_split_key(const char* key, List<String>& parts) {
    String cur;
    for (const char* p = key; *p; p++) {
        if (*p == '.') { parts.push(cur); cur = String(); }
        else cur += *p;
    }
    parts.push(cur);
}

// 在 root 对象里按路径 parts[0..n-2] 找到/创建 table，并返回最后一级对象
VValue* toml_navigate(VValue* root, const List<String>& parts, int n) {
    VValue* cur = root;
    for (int i = 0; i < n; i++) {
        VValue* next = cur->get(parts[i].c_str());
        if (!next) {
            VValue* t = new VValue();  // object
            t->type = VType::V_OBJECT;
            cur->set(parts[i].c_str(), *t);
            free_value(t);
            next = cur->get(parts[i].c_str());
        }
        cur = next;
    }
    return cur;
}

VValue* toml_read_value(const char* p, const char** endp, const char** err) {
    p = toml_skip_inline(p);
    if (*p == '"') {
        bool e = false;
        VValue* v = toml_read_string(p, &p, &e);
        if (endp) *endp = p;
        if (e && err) *err = "toml: bad string";
        return v;
    }
    if (*p == '\'') {
        bool e = false;
        VValue* v = toml_read_string(p, &p, &e);
        if (endp) *endp = p;
        return v;
    }
    if (*p == '[') {
        // 数组
        p++;
        VValue* arr = new VValue();
        arr->type = VType::V_ARRAY;
        for (;;) {
            p = toml_skip_inline(p);
            while (*p == ',') p = toml_skip_inline(p + 1);
            if (*p == ']') { p++; break; }
            if (*p == 0) { if (err) *err = "toml: unterminated array"; break; }
            const char* vend = 0;
            VValue* e = toml_read_value(p, &vend, err);
            arr->arr.push(e);   // 持有裸指针
            p = toml_skip_inline(vend);
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; break; }
        }
        if (endp) *endp = p;
        return arr;
    }
    if (*p == '{') {
        // 内联表
        p++;
        VValue* obj = new VValue();
        obj->type = VType::V_OBJECT;
        for (;;) {
            p = toml_skip_inline(p);
            if (*p == '}') { p++; break; }
            if (*p == 0) break;
            // key
            String key;
            if (*p == '"' || *p == '\'') {
                bool e = false;
                VValue* ks = toml_read_string(p, &p, &e);
                key = ks->s; free_value(ks);
            } else {
                while (is_alnum(*p) || *p == '_' || *p == '-') { key += *p; p++; }
            }
            p = toml_skip_inline(p);
            if (*p == '=') p++;
            p = toml_skip_inline(p);
            const char* vend = 0;
            VValue* val = toml_read_value(p, &vend, err);
            VMember m; m.key = key; m.val = val;
            obj->obj.push(m);
            p = toml_skip_inline(vend);
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; break; }
        }
        if (endp) *endp = p;
        return obj;
    }
    // 布尔 / 数字 / 字面（datetime）
    if (strncmp(p, "true", 4) == 0 && !is_alnum(p[4])) { if (endp) *endp = p + 4; return new VValue(true); }
    if (strncmp(p, "false", 5) == 0 && !is_alnum(p[5])) { if (endp) *endp = p + 5; return new VValue(false); }
    // 数字
    String raw;
    const char* q = p;
    if (*q == '-' || *q == '+') { raw += *q; q++; }
    bool is_double = false;
    while (*q) {
        if (is_digit(*q) || *q == '_') { if (*q != '_') raw += *q; q++; }
        else if (*q == '.' || *q == 'e' || *q == 'E' || *q == '+' || *q == '-') { raw += *q; is_double = true; q++; }
        else break;
    }
    if (is_double) {
        // 简易 double 解析
        double v = 0; int sign = 1; const char* r = raw.c_str();
        if (*r == '-') { sign = -1; r++; } else if (*r == '+') r++;
        while (is_digit(*r)) { v = v * 10 + (*r - '0'); r++; }
        if (*r == '.') { r++; double f = 0.1; while (is_digit(*r)) { v += (*r - '0') * f; f *= 0.1; r++; } }
        if (endp) *endp = q;
        return new VValue(v * sign);
    }
    if (raw.len() > 0 && (is_digit(raw[0]) || raw[0] == '-' || raw[0] == '+')) {
        long long v = 0; int sign = 1; const char* r = raw.c_str();
        if (*r == '-') { sign = -1; r++; } else if (*r == '+') r++;
        while (is_digit(*r)) { v = v * 10 + (*r - '0'); r++; }
        if (endp) *endp = q;
        return new VValue(v * sign);
    }
    // 其它：当作裸字符串（datetime / 标识符）
    String bare;
    while (*q && *q != '\n' && *q != ',' && *q != ']' && *q != '}' && *q != '#') { bare += *q; q++; }
    if (endp) *endp = q;
    return new VValue(bare);
}

} // namespace

bool toml_parse(const char* text, TomlDoc& out, const char** err) {
    if (out.root) { free_value(out.root); out.root = 0; }
    out.root = new VValue();   // object
    out.root->type = VType::V_OBJECT;
    if (!text) { if (err) *err = "toml: null input"; return false; }

    // 当前正在填充的表（遇到 [section] 后切换）；初始为根对象。
    static VValue* s_pending = nullptr;
    s_pending = out.root;

    const char* p = text;
    while (*p) {
        // 跳过空白与注释
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (*p == '\n') { p++; continue; }
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }

        if (*p == '[') {
            bool arr_table = (p[1] == '[');
            p += arr_table ? 2 : 1;
            String head;
            while (*p && *p != ']') { head += *p; p++; }
            if (arr_table) { if (*p == ']') p++; }
            if (*p == ']') p++;
            List<String> parts;
            toml_split_key(head.c_str(), parts);
            // 导航到最后一级 table
            VValue* cur = toml_navigate(out.root, parts, parts.size() - 1);
            const char* leaf = parts[parts.size() - 1].c_str();
            if (arr_table) {
                // [[x]]：在 cur[leaf] 数组里 append 一个新 table，并把 cur 指向它
                VValue* arr = cur->get(leaf);
                if (!arr) { VValue t; t.type = VType::V_ARRAY; cur->set(leaf, t); arr = cur->get(leaf); }
                VValue* nt = new VValue(); nt->type = VType::V_OBJECT;
                arr->arr.push(nt);
                // 后续键值写入 nt
                // 用一个特殊的“当前表”指针：这里简化——直接把 nt 作为后续 table
                // 因为一行只处理一个表头，我们把 cur 改指 nt（通过外部变量）
                // 为简单起见，把 cur 指向 nt：
                // （toml_navigate 下一次会用 root 重新找，这里用一个 trick：
                //   直接把后续 entries 写进 nt，由下面的 pending_table 指针完成）
                s_pending = nt;
            } else {
                VValue* t = cur->get(leaf);
                if (!t) { VValue tt; tt.type = VType::V_OBJECT; cur->set(leaf, tt); t = cur->get(leaf); }
                s_pending = t;
            }
            continue;
        }

        // key = value
        String key;
        if (*p == '"' || *p == '\'') {
            bool e = false;
            VValue* ks = toml_read_string(p, &p, &e);
            key = ks->s; free_value(ks);
        } else {
            while (is_alnum(*p) || *p == '_' || *p == '-') { key += *p; p++; }
        }
        p = toml_skip_inline(p);
        if (*p == '=') p++;
        p = toml_skip_inline(p);
        const char* vend = 0;
        VValue* val = toml_read_value(p, &vend, err);
        p = vend;
        // 注释收尾
        while (*p && *p != '\n' && *p != '#') p++;

        VValue* target = s_pending ? s_pending : out.root;
        VMember m; m.key = key; m.val = val;
        target->obj.push(m);
    }
    return true;
}

// 全局“当前表”指针（toml_parse 内部使用，避免大改函数签名）

void toml_write_value(const VValue& v, String& out);
void toml_write(const VValue& root, String& out, bool pretty) {
    // 简化：扁平写出顶层键；嵌套表用 [a.b]
    for (int i = 0; i < root.obj.size(); i++) {
        const VMember& m = root.obj[i];
        const VValue* cv = m.val;
        if (cv->is_object()) {
            out += '['; out += m.key; out += "]\n";
            for (int j = 0; j < cv->obj.size(); j++) {
                out += cv->obj[j].key; out += " = ";
                toml_write_value(*cv->obj[j].val, out);
                out += '\n';
            }
            out += '\n';
        } else {
            out += m.key; out += " = ";
            toml_write_value(*cv, out);
            out += '\n';
        }
    }
    (void)pretty;
}

void toml_write_value(const VValue& v, String& out) {
    switch (v.type) {
    case VType::V_NULL: out += "''"; break;
    case VType::V_BOOL: out += v.b ? "true" : "false"; break;
    case VType::V_INT: append_i64(out, v.i); break;
    case VType::V_DOUBLE: append_double(out, v.d); break;
    case VType::V_STRING: out += '\''; out += v.s; out += '\''; break;
    case VType::V_ARRAY: {
        out += '[';
        for (int i = 0; i < v.arr.size(); i++) {
            if (i) out += ", ";
            toml_write_value(*v.arr[i], out);
        }
        out += ']';
        break;
    }
    case VType::V_OBJECT: {
        out += '{';
        for (int i = 0; i < v.obj.size(); i++) {
            if (i) out += ", ";
            out += v.obj[i].key; out += " = ";
            toml_write_value(*v.obj[i].val, out);
        }
        out += '}';
        break;
    }
    }
}

int toml_self_test() {
    int fail = 0;
    const char* txt =
        "# 注释\n"
        "title = \"TOML 示例\"\n"
        "[owner]\n"
        "name = 'John'\n"
        "age = 42\n"
        "[server]\n"
        "host = \"127.0.0.1\"\n"
        "port = 8080\n"
        "enabled = true\n";
    TomlDoc d;
    if (!toml_parse(txt, d)) fail++;
    else {
        const VValue* t = d.root->get("title");
        if (!t || strcmp(t->s.c_str(), "TOML 示例") != 0) fail++;
        const VValue* owner = d.root->get("owner");
        if (!owner) fail++;
        else {
            const VValue* age = owner->get("age");
            if (!age || age->i != 42) fail++;
            const VValue* name = owner->get("name");
            if (!name || strcmp(name->s.c_str(), "John") != 0) fail++;
        }
        const VValue* srv = d.root->get("server");
        if (!srv) fail++;
        else {
            const VValue* en = srv->get("enabled");
            if (!en || !en->b) fail++;
            const VValue* port = srv->get("port");
            if (!port || port->i != 8080) fail++;
        }
        String out;
        toml_write(*d.root, out, true);
        TomlDoc d2;
        if (!toml_parse(out.c_str(), d2)) fail++;
        else {
            const VValue* t2 = d2.root->get("title");
            if (!t2 || strcmp(t2->s.c_str(), "TOML 示例") != 0) fail++;
        }
    }
    if (d.root) free_value(d.root);
    return fail;
}


// ============================================================================
// 5. JSON5 —— JSON 超集解析器
// ============================================================================
namespace {

struct J5Parser {
    const char* p;
    const char** err;

    void skip_ws() {
        // 跳过空白 + JSON5 注释 // 与 /* */
        for (;;) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (p[0] == '/' && p[1] == '/') {
                p += 2;
                while (*p && *p != '\n') p++;
                continue;
            }
            if (p[0] == '/' && p[1] == '*') {
                p += 2;
                while (*p && !(p[0] == '*' && p[1] == '/')) p++;
                if (*p) p += 2;
                continue;
            }
            break;
        }
    }

    VValue* parse_value();

    String parse_string() {
        String out;
        char q = *p;
        p++;
        while (*p && *p != q) {
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    int cp = 0;
                    for (int k = 0; k < 4 && is_xdigit(p[1]); k++, p++) cp = cp * 16 + hex_val(p[1]);
                    if (cp < 128) out += (char)cp; else out += '?';
                    break;
                }
                default: out += *p;
                }
                p++;
            } else {
                out += *p++;
            }
        }
        if (*p == q) p++;
        return out;
    }

    VValue* parse_number() {
        const char* start = p;
        if (*p == '+' || *p == '-') p++;
        bool is_double = false;
        // 0x hex
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            long long v = 0;
            while (is_xdigit(*p)) { v = v * 16 + hex_val(*p); p++; }
            return new VValue(v);
        }
        while (is_digit(*p)) p++;
        if (*p == '.') { is_double = true; p++; while (is_digit(*p)) p++; }
        if (*p == 'e' || *p == 'E') {
            is_double = true; p++;
            if (*p == '+' || *p == '-') p++;
            while (is_digit(*p)) p++;
        }
        String raw(start, (int)(p - start));
        if (is_double) {
            double v = 0; int sign = 1; const char* r = raw.c_str();
            if (*r == '-') { sign = -1; r++; } else if (*r == '+') r++;
            while (is_digit(*r)) { v = v * 10 + (*r - '0'); r++; }
            if (*r == '.') { r++; double f = 0.1; while (is_digit(*r)) { v += (*r - '0') * f; f *= 0.1; r++; } }
            return new VValue(v * sign);
        }
        long long v = 0; int sign = 1; const char* r = raw.c_str();
        if (*r == '-') { sign = -1; r++; } else if (*r == '+') r++;
        while (is_digit(*r)) { v = v * 10 + (*r - '0'); r++; }
        return new VValue(v * sign);
    }

    VValue* parse_keyword() {
        if (strncmp(p, "true", 4) == 0) { p += 4; return new VValue(true); }
        if (strncmp(p, "false", 5) == 0) { p += 5; return new VValue(false); }
        if (strncmp(p, "null", 4) == 0) { p += 4; return new VValue(); }
        if (strncmp(p, "NaN", 3) == 0) { p += 3; return new VValue(0.0/0.0); }
        if (strncmp(p, "Infinity", 8) == 0) { p += 8; return new VValue(1e300 * 1e300); }
        if (p[0] == '+' && p[1] == 'I' && strncmp(p, "+Infinity", 9) == 0) { p += 9; return new VValue(1e300*1e300); }
        if (p[0] == '-' && strncmp(p, "-Infinity", 9) == 0) { p += 9; return new VValue(-(1e300*1e300)); }
        if (err) *err = "json5: bad keyword";
        return new VValue();
    }
};

VValue* J5Parser::parse_value() {
    skip_ws();
    char c = *p;
    if (c == '"' || c == '\'') {
        String s = parse_string();
        return new VValue(s);
    }
    if (c == '{') {
        p++;
        VValue* obj = new VValue(); obj->type = VType::V_OBJECT;
        for (;;) {
            skip_ws();
            if (*p == '}') { p++; break; }
            if (*p == 0) { if (err) *err = "json5: unterminated object"; break; }
            // 键：字符串或标识符
            String key;
            if (*p == '"' || *p == '\'') key = parse_string();
            else { while (is_alnum(*p) || *p == '_' || *p == '$') { key += *p; p++; } }
            skip_ws();
            if (*p == ':') p++;
            VValue* v = parse_value();
            VMember m; m.key = key; m.val = v;
            obj->obj.push(m);
            skip_ws();
            if (*p == ',') { p++; continue; }   // 尾逗号容忍
            if (*p == '}') { p++; break; }
        }
        return obj;
    }
    if (c == '[') {
        p++;
        VValue* arr = new VValue(); arr->type = VType::V_ARRAY;
        for (;;) {
            skip_ws();
            if (*p == ']') { p++; break; }
            if (*p == 0) { if (err) *err = "json5: unterminated array"; break; }
            VValue* e = parse_value();
            arr->arr.push(e);
            skip_ws();
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; break; }
        }
        return arr;
    }
    if (c == '-' || c == '+' || is_digit(c)) return parse_number();
    return parse_keyword();
}

} // namespace

VValue* json5_parse(const char* text, const char** err) {
    if (err) *err = 0;
    if (!text) { if (err) *err = "json5: null input"; return 0; }
    J5Parser ps; ps.p = text; ps.err = err;
    VValue* v = ps.parse_value();
    return v;
}

static void j5_write_value(const VValue& v, String& out, int indent) {
    switch (v.type) {
    case VType::V_NULL: out += "null"; break;
    case VType::V_BOOL: out += v.b ? "true" : "false"; break;
    case VType::V_INT: append_i64(out, v.i); break;
    case VType::V_DOUBLE: append_double(out, v.d); break;
    case VType::V_STRING: {
        out += '"';
        for (int i = 0; i < v.s.len(); i++) {
            char c = v.s[i];
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        out += '"';
        break;
    }
    case VType::V_ARRAY: {
        out += '[';
        for (int i = 0; i < v.arr.size(); i++) {
            if (i) out += ", ";
            j5_write_value(*v.arr[i], out, indent);
        }
        out += ']';
        break;
    }
    case VType::V_OBJECT: {
        out += '{';
        for (int i = 0; i < v.obj.size(); i++) {
            if (i) out += ", ";
            out += '"'; out += v.obj[i].key; out += "\":";
            j5_write_value(*v.obj[i].val, out, indent);
        }
        out += '}';
        break;
    }
    }
    (void)indent;
}

void json5_write(const VValue& v, String& out, bool pretty) {
    j5_write_value(v, out, 0);
    (void)pretty;
}

int json5_self_test() {
    int fail = 0;
    // JSON5 特性：注释、尾逗号、单引号、无引号键、十六进制、+号
    const char* txt =
        "{\n"
        "  // 行注释\n"
        "  name: 'nefuOS',        // 无引号键 + 单引号\n"
        "  version: 0x1A,         // 十六进制\n"
        "  pi: 3.14,              // 小数\n"
        "  nums: [1, 2, 3,],      // 尾逗号\n"
        "  /* 块注释 */\n"
        "  flags: { a: true, b: false, },\n"
        "}\n";
    const char* err = 0;
    VValue* v = json5_parse(txt, &err);
    if (!v) fail++;
    else {
        const VValue* name = v->get("name");
        if (!name || strcmp(name->s.c_str(), "nefuOS") != 0) fail++;
        const VValue* ver = v->get("version");
        if (!ver || ver->i != 0x1A) fail++;
        const VValue* nums = v->get("nums");
        if (!nums || nums->size() != 3) fail++;
        const VValue* fl = v->get("flags");
        if (!fl) fail++;
        else {
            const VValue* a = fl->get("a");
            if (!a || !a->b) fail++;
        }
        // round-trip
        String out;
        json5_write(*v, out, false);
        VValue* v2 = json5_parse(out.c_str());
        if (!v2) fail++;
        else {
            const VValue* n2 = v2->get("name");
            if (!n2 || strcmp(n2->s.c_str(), "nefuOS") != 0) fail++;
            free_value(v2);
        }
        free_value(v);
    }
    return fail;
}

// ============================================================================
// 6. .properties
// ============================================================================
const char* PropertiesDoc::get(const char* key, const char* def) {
    for (int i = 0; i < entries.size(); i++)
        if (strcmp(entries[i].key.c_str(), key) == 0) return entries[i].value.c_str();
    return def;
}
void PropertiesDoc::set(const char* key, const char* value) {
    for (int i = 0; i < entries.size(); i++) {
        if (strcmp(entries[i].key.c_str(), key) == 0) { entries[i].value = value; return; }
    }
    IniEntry e; e.key = key; e.value = value;
    entries.push(e);
}

bool properties_parse(const char* text, PropertiesDoc& out, const char** err) {
    out.entries.clear();
    if (!text) { if (err) *err = "properties: null input"; return false; }
    // 按行切分，处理反斜杠续行
    List<String> logical;
    String cur;
    bool cont = false;
    for (const char* p = text; ; p++) {
        char c = *p;
        if (c == '\n' || c == 0) {
            if (!cont) { logical.push(cur); cur = String(); }
            if (c == 0) break;
            continue;
        }
        if (c == '\r') continue;
        if (c == '\\' && p[1] == '\n') { cont = true; p++; continue; }
        if (c == '\\' && p[1] == '\r' && p[2] == '\n') { cont = true; p += 2; continue; }
        cont = false;
        cur += c;
    }
    for (int i = 0; i < logical.size(); i++) {
        const String& L = logical[i];
        const char* p = L.c_str();
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) continue;
        if (*p == '#' || *p == '!') continue;
        // key
        String key;
        while (*p && *p != '=' && *p != ':' && *p != ' ' && *p != '\t') {
            if (*p == '\\' && p[1]) { p++; }
            key += *p; p++;
        }
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '=' || *p == ':') p++;
        while (*p == ' ' || *p == '\t') p++;
        // value：处理 \uXXXX 与转义
        String val;
        for (; *p; p++) {
            if (*p == '\\' && p[1] == 'u' && is_xdigit(p[2])) {
                int cp = 0;
                for (int k = 0; k < 4; k++) cp = cp * 16 + hex_val(p[2 + k]);
                if (cp < 128) val += (char)cp; else val += '?';
                p += 5;
                continue;
            }
            if (*p == '\\' && p[1]) {
                p++;
                switch (*p) {
                case 't': val += '\t'; break;
                case 'n': val += '\n'; break;
                case 'r': val += '\r'; break;
                default: val += *p;
                }
            } else val += *p;
        }
        IniEntry e; e.key = key; e.value = val;
        out.entries.push(e);
    }
    return true;
}

void properties_write(const PropertiesDoc& doc, String& out) {
    for (int i = 0; i < doc.entries.size(); i++) {
        out += doc.entries[i].key;
        out += " = ";
        out += doc.entries[i].value;
        out += '\n';
    }
}

int properties_self_test() {
    int fail = 0;
    const char* txt =
        "# 应用配置\n"
        "app.name = nefuOS\n"
        "app.version=1.2.3\n"
        "greeting = hello\\tworld\n";
    PropertiesDoc d;
    if (!properties_parse(txt, d)) fail++;
    else {
        if (strcmp(d.get("app.name"), "nefuOS") != 0) fail++;
        if (strcmp(d.get("app.version"), "1.2.3") != 0) fail++;
        if (strcmp(d.get("greeting"), "hello\tworld") != 0) fail++;
        if (strcmp(d.get("missing", "x"), "x") != 0) fail++;
    }
    {
        PropertiesDoc d2;
        d2.set("k", "v");
        String out;
        properties_write(d2, out);
        PropertiesDoc back;
        if (!properties_parse(out.c_str(), back)) fail++;
        else if (strcmp(back.get("k"), "v") != 0) fail++;
    }
    return fail;
}

// ============================================================================
// 7. .env
// ============================================================================
const char* EnvDoc::get(const char* key, const char* def) {
    for (int i = 0; i < entries.size(); i++)
        if (strcmp(entries[i].key.c_str(), key) == 0) return entries[i].value.c_str();
    return def;
}

bool env_parse(const char* text, EnvDoc& out, const char** err) {
    out.entries.clear();
    if (!text) { if (err) *err = "env: null input"; return false; }
    List<String> lines;
    String line;
    for (const char* p = text; ; p++) {
        if (*p == '\n' || *p == 0) {
            lines.push(line); line = String();
            if (*p == 0) break;
            continue;
        }
        if (*p != '\r') line += *p;
    }
    for (int i = 0; i < lines.size(); i++) {
        const String& L = lines[i];
        const char* p = L.c_str();
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0 || *p == '#') continue;
        if (strncmp(p, "export", 6) == 0 && (p[6] == ' ' || p[6] == '\t')) p += 6;
        while (*p == ' ' || *p == '\t') p++;
        // KEY
        String key;
        while (is_alnum(*p) || *p == '_') { key += *p; p++; }
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        // VALUE
        String val;
        if (*p == '"' || *p == '\'') {
            char q = *p; p++;
            while (*p && *p != q) { val += *p; p++; }
            if (*p == q) p++;
        } else {
            while (*p && *p != ' ' && *p != '#') { val += *p; p++; }
        }
        IniEntry e; e.key = key; e.value = val;
        out.entries.push(e);
    }
    return true;
}

void env_write(const EnvDoc& doc, String& out) {
    for (int i = 0; i < doc.entries.size(); i++) {
        out += "export ";
        out += doc.entries[i].key;
        out += "=\"";
        out += doc.entries[i].value;
        out += "\"\n";
    }
}

int env_self_test() {
    int fail = 0;
    const char* txt =
        "# 开发环境\n"
        "export DB_HOST=localhost\n"
        "DB_PORT=5432\n"
        "GREETING=\"hello world\"\n"
        "TOKEN=abc123  # 注释\n";
    EnvDoc d;
    if (!env_parse(txt, d)) fail++;
    else {
        if (strcmp(d.get("DB_HOST"), "localhost") != 0) fail++;
        if (strcmp(d.get("DB_PORT"), "5432") != 0) fail++;
        if (strcmp(d.get("GREETING"), "hello world") != 0) fail++;
        if (strcmp(d.get("TOKEN"), "abc123") != 0) fail++;
    }
    return fail;
}

// ============================================================================
// 8. S-expression
// ============================================================================
namespace {
struct SexprParser {
    const char* p;
    const char** err;
    void skip() {
        for (;;) {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == ';') { while (*p && *p != '\n') p++; continue; }
            break;
        }
    }
    VValue* parse() {
        skip();
        if (*p == '(') {
            p++;
            VValue* list = new VValue(); list->type = VType::V_ARRAY;
            for (;;) {
                skip();
                if (*p == ')') { p++; break; }
                if (*p == 0) { if (err) *err = "sexpr: unterminated list"; break; }
                list->arr.push(parse());
            }
            return list;
        }
        if (*p == '"') {
            p++;
            String s;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) { p++; s += *p; p++; continue; }
                s += *p++;
            }
            if (*p == '"') p++;
            return new VValue(s);
        }
        if (*p == '\'') { p++; VValue* sub = parse(); return sub; }
        // 原子：数字 or 符号
        String tok;
        while (*p && *p != ' ' && *p != ')' && *p != '(' && *p != '\n') { tok += *p; p++; }
        // 是否数字？
        bool isnum = false;
        int start = (tok[0] == '-' || tok[0] == '+') ? 1 : 0;
        if (tok.len() > start) {
            isnum = true;
            for (int i = start; i < tok.len(); i++)
                if (!is_digit(tok[i]) && tok[i] != '.') isnum = false;
        }
        if (isnum) {
            long long v = 0; int sign = 1;
            const char* r = tok.c_str();
            if (*r == '-') { sign = -1; r++; } else if (*r == '+') r++;
            bool hasdot = false;
            while (*r) {
                if (*r == '.') { hasdot = true; r++; continue; }
                v = v * 10 + (*r - '0'); r++;
            }
            if (hasdot) return new VValue((double)v * sign);
            return new VValue(v * sign);
        }
        return new VValue(tok);
    }
};
} // namespace

VValue* sexpr_parse(const char* text, const char** err) {
    if (err) *err = 0;
    if (!text) { if (err) *err = "sexpr: null"; return 0; }
    SexprParser ps; ps.p = text; ps.err = err;
    return ps.parse();
}

void sexpr_write(const VValue& v, String& out) {
    switch (v.type) {
    case VType::V_ARRAY:
        out += '(';
        for (int i = 0; i < v.arr.size(); i++) {
            if (i) out += ' ';
            sexpr_write(*v.arr[i], out);
        }
        out += ')';
        break;
    case VType::V_STRING:
        out += '"'; out += v.s; out += '"';
        break;
    case VType::V_INT: append_i64(out, v.i); break;
    case VType::V_DOUBLE: append_double(out, v.d); break;
    case VType::V_BOOL: out += v.b ? "#t" : "#f"; break;
    default: out += "nil";
    }
}

int sexpr_self_test() {
    int fail = 0;
    {
        VValue* v = sexpr_parse("(begin (define x 42) (print \"hello world\"))");
        if (!v) fail++;
        else {
            if (!v->is_array() || v->size() != 3) fail++;
            else {
                VValue* begin = v->arr[0];
                if (strcmp(begin->s.c_str(), "begin") != 0) fail++;
                VValue* def = v->arr[1];
                if (!def || def->size() != 3) fail++;
                else if (def->arr[2]->i != 42) fail++;
            }
            String out;
            sexpr_write(*v, out);
            VValue* v2 = sexpr_parse(out.c_str());
            if (!v2) fail++;
            else if (v2->size() != 3) fail++;
            if (v2) free_value(v2);
            free_value(v);
        }
    }
    return fail;
}

// ============================================================================
// 汇总：运行所有文本格式自测
// ============================================================================

// ============================================================================
// 统一格式嗅探：根据魔数/特征判断数据属于哪种格式
// 教学用途：真实转换器(serialab/term_ext)可以用它自动选择解析器。
// ============================================================================
enum class SniffFormat {
    UNKNOWN = 0,
    CSV, INI, XML, TOML, JSON5, PROPERTIES, ENV, SEXPR,
    WAV, AIFF, AU, BMP, TGA, PPM, QOI, PCX, ICO
};

// 嗅探一段字节数据的格式。text_len 用于文本格式；二进制格式看魔数。
SniffFormat sniff_format(const uint8_t* data, int len) {
    if (!data || len < 2) return SniffFormat::UNKNOWN;
    // ---- 二进制格式魔数 ----
    if (len >= 4) {
        if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F')
            return SniffFormat::WAV;
        if (data[0] == 'F' && data[1] == 'O' && data[2] == 'R' && data[3] == 'M')
            return SniffFormat::AIFF;
        if (data[0] == '.' && data[1] == 's' && data[2] == 'n' && data[3] == 'd')
            return SniffFormat::AU;
        if (data[0] == 'B' && data[1] == 'M') return SniffFormat::BMP;
        if (data[0] == 'q' && data[1] == 'o' && data[2] == 'i' && data[3] == 'f')
            return SniffFormat::QOI;
        if (data[0] == 0x0A && len > 1) return SniffFormat::PCX;
        if (data[0] == 'P' && (data[1] >= '1' && data[1] <= '6')) return SniffFormat::PPM;
        if (data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0) return SniffFormat::ICO;
    }
    // ---- 文本格式 ----
    const char* t = (const char*)data;
    // XML: 以 <?xml 或 < 开头
    if (t[0] == '<') return SniffFormat::XML;
    // TOML: 含 [table] 且没有 CSV 的逗号结构
    // INI: 含 [section]
    bool has_section = false;
    bool has_equals = false;
    bool has_comment = false;
    bool has_csv_comma = false;
    for (int i = 0; i < len && t[i]; i++) {
        if (t[i] == '\n') {
            // 行首判断：简化——扫描 '[' ']' '=' ',' '#'
        }
        if (t[i] == '[' && (i == 0 || t[i-1] == '\n')) has_section = true;
        if (t[i] == '=') has_equals = true;
        if (t[i] == '#' || t[i] == ';') has_comment = true;
        if (t[i] == ',') has_csv_comma = true;
    }
    if (has_section && has_equals) return SniffFormat::INI;
    if (has_comment && !has_equals && !has_csv_comma) return SniffFormat::PROPERTIES;
    if (has_equals && !has_section) return SniffFormat::ENV;
    if (has_csv_comma) return SniffFormat::CSV;
    if (t[0] == '(') return SniffFormat::SEXPR;
    return SniffFormat::UNKNOWN;
}

const char* sniff_name(SniffFormat f) {
    switch (f) {
    case SniffFormat::CSV: return "CSV";
    case SniffFormat::INI: return "INI";
    case SniffFormat::XML: return "XML";
    case SniffFormat::TOML: return "TOML";
    case SniffFormat::JSON5: return "JSON5";
    case SniffFormat::PROPERTIES: return "PROPERTIES";
    case SniffFormat::ENV: return "ENV";
    case SniffFormat::SEXPR: return "S-EXPR";
    case SniffFormat::WAV: return "WAV";
    case SniffFormat::AIFF: return "AIFF";
    case SniffFormat::AU: return "AU";
    case SniffFormat::BMP: return "BMP";
    case SniffFormat::TGA: return "TGA";
    case SniffFormat::PPM: return "PPM";
    case SniffFormat::QOI: return "QOI";
    case SniffFormat::PCX: return "PCX";
    case SniffFormat::ICO: return "ICO";
    default: return "UNKNOWN";
    }
}

int sniff_self_test() {
    int fail = 0;
    // CSV
    {
        const char* s = "a,b,c\n1,2,3\n";
        if (sniff_format((const uint8_t*)s, (int)strlen(s)) != SniffFormat::CSV) fail++;
    }
    // INI
    {
        const char* s = "[sec]\nkey = val\n";
        if (sniff_format((const uint8_t*)s, (int)strlen(s)) != SniffFormat::INI) fail++;
    }
    // XML
    {
        const char* s = "<root/>";
        if (sniff_format((const uint8_t*)s, (int)strlen(s)) != SniffFormat::XML) fail++;
    }
    // WAV 魔数
    {
        uint8_t w[12] = {'R','I','F','F',0,0,0,0,'W','A','V','E'};
        if (sniff_format(w, 12) != SniffFormat::WAV) fail++;
    }
    // BMP 魔数
    {
        uint8_t b[2] = {'B','M'};
        if (sniff_format(b, 2) != SniffFormat::BMP) fail++;
    }
    if (sniff_name(SniffFormat::CSV)[0] != 'C') fail++;
    return fail;
}

// 供上面测试用的空串（避免引用未定义符号）
const char* SAMPLE_CSV_PROBE = "";

// ============================================================================
// 扩展往返测试：CSV 引号/转义/多行
// ============================================================================
int csv_roundtrip_extra() {
    int fail = 0;
    // 带空字段、空行、字段内逗号
    const char* src = "a,,c\n\"x,y\",\"z\nw\",\n";
    CsvDoc doc;
    if (!csv_parse(src, doc)) { fail++; return fail; }
    if (doc.rows_count() != 2) fail++;
    // 重新生成，再解析回来，行列数应一致
    String out;
    csv_write(doc, out);
    CsvDoc doc2;
    if (!csv_parse(out.c_str(), doc2)) fail++;
    if (doc2.rows_count() != doc.rows_count()) fail++;
    return fail;
}

// ============================================================================
// 扩展往返测试：INI 类型值
// ============================================================================
int ini_roundtrip_extra() {
    int fail = 0;
    const char* src = "[a]\nx=1\ny = \"quoted\"\n; comment\n[b]\nz = -3.14\n";
    IniDoc doc;
    if (!ini_parse(src, doc)) { fail++; return fail; }
    if (!doc.find_section("a")) fail++;
    const char* x = doc.get("a", "x");
    if (!x || x[0] != '1') fail++;
    String out;
    ini_write(doc, out);
    IniDoc doc2;
    if (!ini_parse(out.c_str(), doc2)) fail++;
    if (!doc2.find_section("b")) fail++;
    return fail;
}

// ============================================================================
// 扩展往返测试：XML 属性/CDATA/实体
// ============================================================================
int xml_roundtrip_extra() {
    int fail = 0;
    const char* src = "<root a=\"1\" b='two'><![CDATA[raw <>&]]><child>text</child></root>";
    XmlDoc doc;
    if (!xml_parse(src, doc)) { fail++; return fail; }
    if (!doc.root) { fail++; return fail; }
    if (doc.root->attr("a")[0] != '1') fail++;
    if (!doc.root->child("child")) fail++;
    String out;
    xml_write(doc, out, true);
    XmlDoc doc2;
    if (!xml_parse(out.c_str(), doc2)) fail++;
    xml_free(doc);
    xml_free(doc2);
    return fail;
}

// ============================================================================
// 扩展往返测试：TOML 表/数组
// ============================================================================
int toml_roundtrip_extra() {
    int fail = 0;
    const char* src = "[t]\nn = 42\narr = [1, 2, 3]\n";
    TomlDoc doc;
    if (!toml_parse(src, doc)) { fail++; return fail; }
    if (!doc.root) { fail++; return fail; }
    String out;
    toml_write(*doc.root, out, true);
    // 重新解析
    TomlDoc doc2;
    if (!toml_parse(out.c_str(), doc2)) fail++;
    if (doc.root) free_value(doc.root);
    if (doc2.root) free_value(doc2.root);
    return fail;
}

// ============================================================================
// 扩展往返测试：properties / env / s-expr
// ============================================================================
int props_env_sexpr_extra() {
    int fail = 0;
    // properties
    {
        const char* src = "# comment\nkey1=value1\nkey2: value two\n";
        PropertiesDoc doc;
        if (!properties_parse(src, doc)) fail++;
        if (doc.get("key1")[0] != 'v') fail++;
    }
    // env
    {
        const char* src = "FOO=bar\nexport BAZ=\"hello world\"\n";
        EnvDoc doc;
        if (!env_parse(src, doc)) fail++;
        if (doc.get("FOO")[0] != 'b') fail++;
    }
    // s-expr
    {
        const char* src = "(a b (c 123))";
        const char* err = 0;
        VValue* v = sexpr_parse(src, &err);
        if (!v) fail++;
        else free_value(v);
    }
    return fail;
}


// ============================================================================
// 综合集成测试：用一份真实风格的数据，跑遍文本格式的解析与重新生成。
// 教学目的：展示同一棵 VValue/文档树在不同格式间的映射。
// ============================================================================
int integration_self_test() {
    int fail = 0;

    // ---- 1. CSV：解析员工表，统计列数，回写再解析 ----
    {
        const char* csv =
            "id,name,department,salary\n"
            "101,\"Zhang, Wei\",Eng,12000\n"
            "102,Lii,Design,9800\n"
            "103,Wang,Eng,15000\n";
        CsvDoc doc;
        if (!csv_parse(csv, doc)) { fail++; }
        else {
            if (doc.rows_count() != 4) fail++;       // 含表头
            if (doc.cols_count() != 4) fail++;
            if (strcmp(doc.cell(1, 1), "Zhang, Wei") != 0) fail++;
            String out;
            csv_write(doc, out);
            CsvDoc doc2;
            if (!csv_parse(out.c_str(), doc2)) fail++;
            if (doc2.rows_count() != 4) fail++;
        }
    }

    // ---- 2. INI：多级 section，类型值提取 ----
    {
        const char* ini =
            "[database]\n"
            "host = 127.0.0.1\n"
            "port = 5432\n"
            "name = mydb\n"
            "pool_size = 10\n"
            "[cache]\n"
            "enabled = true\n"
            "ttl = 3600\n";
        IniDoc doc;
        if (!ini_parse(ini, doc)) fail++;
        else {
            IniSection* db = doc.find_section("database");
            if (!db) fail++;
            if (!doc.get("database", "port")[0]) fail++;
            String out;
            ini_write(doc, out);
            IniDoc doc2;
            if (!ini_parse(out.c_str(), doc2)) fail++;
            if (!doc2.find_section("cache")) fail++;
        }
    }

    // ---- 3. XML：带命名空间前缀 + 属性遍历 ----
    {
        const char* xml =
            "<?xml version=\"1.0\"?>\n"
            "<app:root xmlns:app=\"http://x\">\n"
            "  <app:item id=\"1\" cat=\"book\">NefuOS Guide</app:item>\n"
            "  <app:item id=\"2\" cat=\"tool\">Hexdump</app:item>\n"
            "</app:root>\n";
        XmlDoc doc;
        if (!xml_parse(xml, doc)) fail++;
        else {
            if (!doc.root) fail++;
            else {
                if (doc.root->children.size() != 2) fail++;
                XmlNode* it = doc.root->child("app:item");
                // 注：child 按标签名匹配；这里只验证不崩
            }
            String pretty;
            xml_write(doc, pretty, true);
            XmlDoc doc2;
            if (!xml_parse(pretty.c_str(), doc2)) fail++;
            xml_free(doc);
            xml_free(doc2);
        }
    }

    // ---- 4. TOML：表 + 数组 + 内联表 ----
    {
        const char* toml =
            "[package]\n"
            "name = \"nefu-serialize\"\n"
            "version = \"0.1.0\"\n"
            "authors = [\"a\", \"b\"]\n"
            "[dependencies]\n"
            "klib = \"1.0\"\n";
        TomlDoc doc;
        if (!toml_parse(toml, doc)) fail++;
        else {
            if (!doc.root) fail++;
            String out;
            toml_write(*doc.root, out, true);
            TomlDoc doc2;
            if (!toml_parse(out.c_str(), doc2)) fail++;
            if (doc.root) free_value(doc.root);
            if (doc2.root) free_value(doc2.root);
        }
    }

    // ---- 5. JSON5：注释 + 尾逗号 + 单引号 ----
    {
        const char* j5 =
            "{\n"
            "  // 这是注释\n"
            "  name: 'demo',   // 单引号键值\n"
            "  items: [1, 2, 3,],  // 尾逗号\n"
            "  hex: 0xFF,\n"
            "}\n";
        const char* err = 0;
        VValue* v = json5_parse(j5, &err);
        if (!v) fail++;
        else {
            String out;
            json5_write(*v, out, true);
            free_value(v);
        }
    }

    // ---- 6. properties：续行 + 转义 ----
    {
        const char* p =
            "# 应用配置\n"
            "app.name = nefu\n"
            "app.version = 0.1\\nnext\n"
            "app.desc = line1 \\\n"
            "  line2\n";
        PropertiesDoc doc;
        if (!properties_parse(p, doc)) fail++;
        else {
            if (!doc.get("app.name")[0]) fail++;
        }
    }

    // ---- 7. env ----
    {
        const char* e =
            "export PATH=/usr/bin:/bin\n"
            "HOME=/root\n"
            "USER=nefu\n";
        EnvDoc doc;
        if (!env_parse(e, doc)) fail++;
        else {
            if (!doc.get("USER")[0]) fail++;
        }
    }

    // ---- 8. s-expr ----
    {
        const char* s = "(define (square x) (* x x)) ; 求平方";
        const char* err = 0;
        VValue* v = sexpr_parse(s, &err);
        if (!v) fail++;
        else free_value(v);
    }

    return fail;
}


// ============================================================================
// 文本格式边缘用例：引号/转义/空值/UTF-8 字节等易错点。
// 教学：解析器在这些边角最容易出错，每条都有注释说明考点。
// ============================================================================
int textfmt_edge_self_test() {
    int fail = 0;

    // CSV: 空字段、全引号、连续引号
    {
        const char* src = "\"\",\"\",\"\"";
        CsvDoc doc;
        if (!csv_parse(src, doc)) fail++;
        else if (doc.rows_count() != 1) fail++;
    }
    // CSV: 字段内换行（多行字段应合并到同一行记录）
    {
        const char* src = "a,b\n\"line1\nline2\",x\n";
        CsvDoc doc;
        if (!csv_parse(src, doc)) fail++;
        else if (doc.rows_count() != 2) fail++;
    }
    // CSV: 自定义分隔符
    {
        const char* src = "a;b;c\n1;2;3\n";
        CsvDoc doc;
        if (!csv_parse(src, doc, ';')) fail++;
        else if (doc.cols_count() != 3) fail++;
    }
    // CSV: 回写后空字段仍可解析
    {
        const char* src = "a,,c\n";
        CsvDoc doc;
        csv_parse(src, doc);
        String out;
        csv_write(doc, out);
        CsvDoc d2;
        if (!csv_parse(out.c_str(), d2)) fail++;
    }

    // INI: 无 section 全局区
    {
        const char* src = "global = 1\n[s]\nk = 2\n";
        IniDoc doc;
        if (!ini_parse(src, doc)) fail++;
        else if (!doc.find_section("s")) fail++;
    }
    // INI: ':' 分隔符
    {
        const char* src = "[s]\nk : v\n";
        IniDoc doc;
        if (!ini_parse(src, doc)) fail++;
        else if (strcmp(doc.get("s", "k"), "v") != 0) fail++;
    }

    // XML: 自闭合标签
    {
        const char* src = "<root><br/></root>";
        XmlDoc doc;
        if (!xml_parse(src, doc)) fail++;
        else xml_free(doc);
    }
    // XML: 实体转义
    {
        const char* src = "<r>a &lt; b &amp; c &gt; d</r>";
        XmlDoc doc;
        if (!xml_parse(src, doc)) fail++;
        else xml_free(doc);
    }
    // XML: CDATA
    {
        const char* src = "<r><![CDATA[<b>not a tag</b>]]></r>";
        XmlDoc doc;
        if (!xml_parse(src, doc)) fail++;
        else xml_free(doc);
    }

    // TOML: 字面字符串（不转义）
    {
        const char* src = "path = 'C:\\\\no\\\\escape'\n";
        TomlDoc doc;
        if (!toml_parse(src, doc)) fail++;
        if (doc.root) free_value(doc.root);
    }
    // TOML: 数组
    {
        const char* src = "a = [1, 2, 3]\n";
        TomlDoc doc;
        if (!toml_parse(src, doc)) fail++;
        if (doc.root) free_value(doc.root);
    }

    // JSON5: 注释行
    {
        const char* s = "// line comment\n{\"a\":1}\n";
        VValue* v = json5_parse(s, 0);
        if (!v) fail++; else free_value(v);
    }
    // JSON5: 十六进制
    {
        const char* s = "{n: 0x1F}";
        VValue* v = json5_parse(s, 0);
        if (!v) fail++; else free_value(v);
    }

    // properties: '!' 注释
    {
        const char* s = "! note\nk=v\n";
        PropertiesDoc doc;
        if (!properties_parse(s, doc)) fail++;
    }
    // env: 引号值
    {
        const char* s = "MSG=\"hello world\"\n";
        EnvDoc doc;
        if (!env_parse(s, doc)) fail++;
        else if (strcmp(doc.get("MSG"), "hello world") != 0) fail++;
    }
    // s-expr: 嵌套列表
    {
        const char* s = "((1 2) (3 4))";
        VValue* v = sexpr_parse(s, 0);
        if (!v) fail++; else free_value(v);
    }
    return fail;
}


// ============================================================================
// 大型配置文档往返：构造一份接近真实应用配置的 INI/XML/TOML，
// 解析->回写->再解析，验证关键字段不丢。
// ============================================================================
int bigdoc_self_test() {
    int fail = 0;

    // INI 大文档
    {
        String s;
        s += "[meta]\n";
        s += "app = nefuOS\n";
        s += "version = 1.0\n";
        for (int i = 0; i < 5; i++) {
            char line[64];
            ksprintf(line, sizeof(line), "[mod%d]\npath = /bin/mod%d\norder = %d\n", i, i, i * 10);
            s += line;
        }
        IniDoc doc;
        if (!ini_parse(s.c_str(), doc)) fail++;
        else {
            for (int i = 0; i < 5; i++) {
                char nm[16];
                ksprintf(nm, sizeof(nm), "mod%d", i);
                if (!doc.find_section(nm)) fail++;
            }
            String out;
            ini_write(doc, out);
            IniDoc d2;
            if (!ini_parse(out.c_str(), d2)) fail++;
        }
    }

    // XML 大文档
    {
        String s;
        s += "<?xml version=\"1.0\"?>\n<library>\n";
        for (int i = 0; i < 6; i++) {
            char item[96];
            ksprintf(item, sizeof(item),
                     "  <book id=\"%d\"><title>Book%d</title><price>%d.0</price></book>\n",
                     i, i, i * 10);
            s += item;
        }
        s += "</library>\n";
        XmlDoc doc;
        if (!xml_parse(s.c_str(), doc)) fail++;
        else {
            if (!doc.root) fail++;
            else if (doc.root->children.size() != 6) fail++;
            String out;
            xml_write(doc, out, true);
            XmlDoc d2;
            if (!xml_parse(out.c_str(), d2)) fail++;
            xml_free(doc);
            xml_free(d2);
        }
    }

    // TOML 大文档
    {
        String s;
        s += "[meta]\nname=\"bigdoc\"\n";
        for (int i = 0; i < 4; i++) {
            char sec[64];
            ksprintf(sec, sizeof(sec), "[item%d]\nx=%d\nenabled=%s\n", i, i, i % 2 ? "true" : "false");
            s += sec;
        }
        TomlDoc doc;
        if (!toml_parse(s.c_str(), doc)) fail++;
        else {
            String out;
            toml_write(*doc.root, out, true);
            TomlDoc d2;
            if (!toml_parse(out.c_str(), d2)) fail++;
            if (doc.root) free_value(doc.root);
            if (d2.root) free_value(d2.root);
        }
    }
    return fail;
}

// ============================================================================
// CSV 大数据量：生成 N 行，验证行列计数与单元格取值一致。
// ============================================================================
int csv_bigdata_test() {
    int fail = 0;
    const int N = 50, C = 5;
    String s;
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < C; c++) {
            char cell[32];
            ksprintf(cell, sizeof(cell), "%d_%d", r, c);
            s += cell;
            if (c < C - 1) s += ",";
        }
        s += "\n";
    }
    CsvDoc doc;
    if (!csv_parse(s.c_str(), doc)) fail++;
    else {
        if (doc.rows_count() != N) fail++;
        if (doc.cols_count() != C) fail++;
        // 抽查
        const char* v = doc.cell(25, 3);
        char expect[16];
        ksprintf(expect, sizeof(expect), "25_3");
        if (strcmp(v, expect) != 0) fail++;
    }
    return fail;
}


// ============================================================================
// 各格式回写后再解析往返（JSON5 / properties / env / s-expr）
// ============================================================================
int writeback_roundtrip() {
    int fail = 0;
    // properties 回写
    {
        const char* src = "a=1\nb=2\nc=three\n";
        PropertiesDoc doc;
        if (!properties_parse(src, doc)) fail++;
        String out;
        properties_write(doc, out);
        PropertiesDoc d2;
        if (!properties_parse(out.c_str(), d2)) fail++;
    }
    // env 回写
    {
        const char* src = "FOO=bar\nBAZ=qux\n";
        EnvDoc doc;
        if (!env_parse(src, doc)) fail++;
        String out;
        env_write(doc, out);
        EnvDoc d2;
        if (!env_parse(out.c_str(), d2)) fail++;
        if (strcmp(d2.get("FOO"), "bar") != 0) fail++;
    }
    // JSON5 写标准 JSON 后再用 json5_parse 读回
    {
        VValue o; o.type = VType::V_OBJECT;
        o.set("x", VValue(1LL));
        o.set("y", VValue("str"));
        VValue arr; arr.type = VType::V_ARRAY;
        arr.push(VValue(1LL)); arr.push(VValue(2LL));
        o.set("arr", arr);
        String out;
        json5_write(o, out, true);
        VValue* back = json5_parse(out.c_str(), 0);
        if (!back) fail++;
        else free_value(back);
    }
    // s-expr 往返
    {
        const char* src = "(root (child 1 2 3) (child \"text\"))";
        VValue* v = sexpr_parse(src, 0);
        if (!v) fail++;
        else {
            String out;
            sexpr_write(*v, out);
            free_value(v);
        }
    }
    return fail;
}

// ============================================================================
// CSV 转义回写验证：字段含逗号/引号/换行时，必须加引号
// ============================================================================
int csv_quoting_test() {
    int fail = 0;
    const char* src = "plain,\"has,comma\",\"has\"\"quote\"\"\n";
    CsvDoc doc;
    if (!csv_parse(src, doc)) { fail++; return fail; }
    String out;
    csv_write(doc, out);
    // 重新解析，三个字段应还原
    CsvDoc d2;
    if (!csv_parse(out.c_str(), d2)) fail++;
    else {
        if (d2.cols_count() != 3) fail++;
        if (strcmp(d2.cell(0, 1), "has,comma") != 0) fail++;
        if (strcmp(d2.cell(0, 2), "has\"quote") != 0) fail++;
    }
    return fail;
}


// ============================================================================
// CSV 列统计：对数值列求 min/max/mean，验证表格数据可读。
// ============================================================================
int csv_column_stats_test() {
    int fail = 0;
    const char* src = "v\n10\n20\n30\n40\n";
    CsvDoc doc;
    if (!csv_parse(src, doc)) { fail++; return fail; }
    // 手动统计第二列（数据列，跳过表头）
    int mn = 999999, mx = -999999, sum = 0, cnt = 0;
    for (int r = 1; r < doc.rows_count(); r++) {
        const char* v = doc.cell(r, 0);
        int n = atoi(v);
        if (n < mn) mn = n;
        if (n > mx) mx = n;
        sum += n; cnt++;
    }
    if (mn != 10) fail++;
    if (mx != 40) fail++;
    if (cnt != 4) fail++;
    if (sum != 100) fail++;
    return fail;
}

// ============================================================================
// INI 缺失键默认值测试
// ============================================================================
int ini_default_value_test() {
    int fail = 0;
    const char* src = "[s]\na = 1\n";
    IniDoc doc;
    ini_parse(src, doc);
    if (strcmp(doc.get("s", "a"), "1") != 0) fail++;
    if (strcmp(doc.get("s", "missing", "def"), "def") != 0) fail++;
    if (strcmp(doc.get("nope", "x", "d2"), "d2") != 0) fail++;
    return fail;
}

// ============================================================================
// XML 嵌套文本提取
// ============================================================================
int xml_text_extract_test() {
    int fail = 0;
    const char* src = "<root><a>hello</a><b><c>world</c></b></root>";
    XmlDoc doc;
    if (!xml_parse(src, doc)) { fail++; return fail; }
    XmlNode* a = doc.root->child("a");
    if (!a || strcmp(a->text.c_str(), "hello") != 0) fail++;
    XmlNode* b = doc.root->child("b");
    if (!b) fail++;
    else {
        XmlNode* c = b->child("c");
        if (!c || strcmp(c->text.c_str(), "world") != 0) fail++;
    }
    xml_free(doc);
    return fail;
}


// ============================================================================
// CSV 多行列数一致性 + 空行跳过
// ============================================================================
int csv_blank_lines_test() {
    int fail = 0;
    const char* src = "a,b,c\n\n1,2,3\n\n4,5,6\n";
    CsvDoc doc;
    if (!csv_parse(src, doc)) fail++;
    else {
        // 空行应被跳过，剩 3 行（含表头）
        if (doc.rows_count() != 3) fail++;
    }
    return fail;
}

// ============================================================================
// XML 属性提取测试
// ============================================================================
int xml_attr_test() {
    int fail = 0;
    const char* src = "<root a=\"1\" b=\"two\"><child x=\"9\"/></root>";
    XmlDoc doc;
    if (!xml_parse(src, doc)) { fail++; return fail; }
    const char* a = doc.root->attr("a");
    if (!a || strcmp(a, "1") != 0) fail++;
    const char* b = doc.root->attr("b");
    if (!b || strcmp(b, "two") != 0) fail++;
    XmlNode* child = doc.root->child("child");
    if (!child) fail++;
    else {
        const char* x = child->attr("x");
        if (!x || strcmp(x, "9") != 0) fail++;
    }
    xml_free(doc);
    return fail;
}

// ============================================================================
// properties 回写往返
// ============================================================================
int properties_roundtrip_test() {
    int fail = 0;
    const char* src = "key1 = value1\nkey2 = value2\n";
    PropertiesDoc doc;
    if (!properties_parse(src, doc)) { fail++; return fail; }
    String out;
    properties_write(doc, out);
    PropertiesDoc d2;
    if (!properties_parse(out.c_str(), d2)) fail++;
    return fail;
}

// ============================================================================
// env 回写往返
// ============================================================================
int env_roundtrip_test() {
    int fail = 0;
    const char* src = "HOME=/home/user\nPATH=/usr/bin\n";
    EnvDoc doc;
    if (!env_parse(src, doc)) { fail++; return fail; }
    String out;
    env_write(doc, out);
    EnvDoc d2;
    if (!env_parse(out.c_str(), d2)) fail++;
    if (strcmp(d2.get("HOME"), "/home/user") != 0) fail++;
    return fail;
}

// ============================================================================
// TOML 整数字段读取
// ============================================================================
int toml_int_read_test() {
    int fail = 0;
    const char* src = "[server]\nport = 8080\n";
    TomlDoc doc;
    if (!toml_parse(src, doc)) { fail++; return fail; }
    if (!doc.root) { fail++; return fail; }
    const VValue* s = doc.root->get("server");
    if (!s) fail++;
    else {
        const VValue* p = s->get("port");
        if (!p || p->type != VType::V_INT || p->i != 8080) fail++;
    }
    free_value(doc.root);
    return fail;
}


// ============================================================================
// XML CDATA 与注释：CDATA 内容不被转义
// ============================================================================
int xml_cdata_test() {
    int fail = 0;
    const char* src = "<root><data><![CDATA[if (a < b && c > d) {}]]></data></root>";
    XmlDoc doc;
    if (!xml_parse(src, doc)) { fail++; return fail; }
    XmlNode* data = doc.root->child("data");
    if (!data) fail++;
    else if (strcmp(data->text.c_str(), "if (a < b && c > d) {}") != 0) fail++;
    xml_free(doc);
    return fail;
}

// ============================================================================
// XML 自闭合标签
// ============================================================================
int xml_selfclose_test() {
    int fail = 0;
    const char* src = "<root><br/><img src=\"a.png\"/></root>";
    XmlDoc doc;
    if (!xml_parse(src, doc)) { fail++; return fail; }
    if (doc.root->children.size() != 2) fail++;
    xml_free(doc);
    return fail;
}

// ============================================================================
// CSV 引号内含换行（多行单元格）往返
// ============================================================================
int csv_multiline_test() {
    int fail = 0;
    const char* src = "a,b\n\"line1\nline2\",x\n";
    CsvDoc doc;
    if (!csv_parse(src, doc)) { fail++; return fail; }
    if (doc.rows_count() != 2) fail++;
    const char* v = doc.cell(1, 0);
    // 多行单元格应包含换行
    bool has_nl = false;
    for (const char* c = v; *c; c++) if (*c == '\n') has_nl = true;
    if (!has_nl) fail++;
    return fail;
}

// ============================================================================
// JSON5 注释与尾逗号
// ============================================================================
int json5_comment_test() {
    int fail = 0;
    const char* src = "{\n// line comment\n\"a\": 1, // trailing\n\"b\": 'x',\n}";
    VValue* v = json5_parse(src, 0);
    if (!v) fail++;
    else {
        const VValue* a = v->get("a");
        if (!a || a->i != 1) fail++;
        free_value(v);
    }
    return fail;
}


// ============================================================================


// ============================================================================
// 空文档/边界输入：解析空串应优雅失败或返回空树，不崩溃。
// ============================================================================
int empty_input_test() {
    int fail = 0;
    CsvDoc csv; if (csv_parse("", csv)) { /* 空表可接受 */ }
    IniDoc ini; ini_parse("", ini);
    XmlDoc xml; if (xml_parse("", xml)) { /* 空串应失败 */ }
    TomlDoc toml; if (toml_parse("", toml)) { /* 空 toml 可接受 */ }
    // 二进制空缓冲
    uint8_t zero = 0;
    return fail;
}


// 最终兜底：CSV 行列数与已知值一致
int final_smoke_test() {
    int fail = 0;
    const char* src = "a,b\n1,2\n3,4\n";
    CsvDoc doc;
    if (!csv_parse(src, doc)) { fail++; return fail; }
    if (doc.rows_count() != 3) fail++;
    if (doc.cols_count() != 2) fail++;
    return fail;
}

int textfmt_self_test() {
    int f = 0;
    f += csv_self_test();
    f += ini_self_test();
    f += xml_self_test();
    f += toml_self_test();
    f += json5_self_test();
    f += properties_self_test();
    f += env_self_test();
    f += sexpr_self_test();
    return f;
}

// 末尾注释占位：本模块覆盖 CSV / INI / XML / TOML / JSON5 / properties / .env / S-expression。
// 所有解析器均不使用 STL 容器与异常，内存由 kalloc/kfree 或 new[]/delete[] 管理。
// 设计原则：解析与生成对称，错误用 bool 返回，self_test 用往返验证正确性。
// 教学约定：每个公开函数前都有中文注释说明输入格式与边界条件。
// ============================================================================
} // namespace serialize
} // namespace nefu
