// nefuOS JSON library — implementation
// Recursive-descent parser + serializer.
#include "json.h"

namespace nefu {
namespace json {

// ===================== Value object accessors =====================

int Value::size() const {
    if (is_array()) return arr.size();
    if (is_object()) return obj.size();
    return 0;
}

const Value* Value::get(const char* key) const {
    for (int i = 0; i < obj.size(); i++)
        if (obj[i].key == key) return &obj[i].val;
    return 0;
}

Value* Value::get(const char* key) {
    for (int i = 0; i < obj.size(); i++)
        if (obj[i].key == key) return &obj[i].val;
    return 0;
}

bool Value::has(const char* key) const { return get(key) != 0; }

void Value::set(const char* key, const Value& v) {
    for (int i = 0; i < obj.size(); i++)
        if (obj[i].key == key) { obj[i].val = v; return; }
    obj.push(Member(String(key), v));
}

const char* Value::get_str(const char* key, const char* def) const {
    const Value* v = get(key);
    return (v && v->is_string()) ? v->s.c_str() : def;
}

long long Value::get_int(const char* key, long long def) const {
    const Value* v = get(key);
    return (v && v->is_int()) ? v->i : def;
}

bool Value::get_bool(const char* key, bool def) const {
    const Value* v = get(key);
    return (v && v->is_bool()) ? v->b : def;
}

// ===================== int64 <-> string =====================

long long str_to_i64(const char* s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') s++;
    unsigned long long v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (unsigned long long)(*s - '0');
        s++;
    }
    if (neg) return (long long)(0 - v);
    return (long long)v;
}

void i64_to_str(long long v, String& out) {
    if (v == 0) { out += '0'; return; }
    char tmp[24];
    int n = 0;
    bool neg = v < 0;
    unsigned long long u = neg ? (unsigned long long)(0 - v) : (unsigned long long)v;
    while (u > 0) {
        tmp[n++] = (char)('0' + (int)(u % 10));
        u /= 10;
    }
    if (neg) tmp[n++] = '-';
    while (n > 0) out += tmp[--n];
}

// ===================== parser =====================

namespace {

struct Parser {
    const char* p;
    const char* err;
    bool fail;

    Parser(const char* s) : p(s), err(0), fail(false) {}

    void skip_ws() {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    }

    bool fail_at(const char* msg) {
        if (!fail) { fail = true; err = msg; }
        return false;
    }

    bool parse_value(Value& out) {
        skip_ws();
        if (fail) return false;
        char c = *p;
        if (c == 0) return fail_at("unexpected end of input");
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') { String s; if (!parse_string(s)) return false; out = Value(s); return true; }
        if (c == 't') { if (!expect_lit("true")) return false; out = Value(true); return true; }
        if (c == 'f') { if (!expect_lit("false")) return false; out = Value(false); return true; }
        if (c == 'n') { if (!expect_lit("null")) return false; out = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
        return fail_at("unexpected character");
    }

    bool expect_lit(const char* lit) {
        const char* q = lit;
        while (*q) {
            if (*p != *q) return fail_at("bad literal");
            p++; q++;
        }
        return true;
    }

    bool parse_string(String& out) {
        if (*p != '"') return fail_at("expected string");
        p++; // opening quote
        out.clear();
        while (*p && *p != '"') {
            char c = *p;
            if (c == '\\') {
                p++;
                if (!*p) return fail_at("bad escape");
                char e = *p;
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    // \uXXXX -> encode UTF-8 (basic plane only; surrogate pairs
                    // are approximated by emitting the code point bytes)
                    p++;
                    unsigned cp = 0;
                    for (int k = 0; k < 4; k++) {
                        char h = *p;
                        int d;
                        if (h >= '0' && h <= '9') d = h - '0';
                        else if (h >= 'a' && h <= 'f') d = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') d = h - 'A' + 10;
                        else return fail_at("bad \\u escape");
                        cp = (cp << 4) | (unsigned)d;
                        p++;
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    continue;
                }
                default: return fail_at("bad escape");
                }
                p++;
            } else {
                out += c;
                p++;
            }
        }
        if (*p != '"') return fail_at("unterminated string");
        p++; // closing quote
        return true;
    }

    bool parse_number(Value& out) {
        const char* start = p;
        if (*p == '-') p++;
        while (*p >= '0' && *p <= '9') p++;
        bool is_float = false;
        if (*p == '.') { is_float = true; p++; while (*p >= '0' && *p <= '9') p++; }
        if (*p == 'e' || *p == 'E') {
            is_float = true;
            p++;
            if (*p == '+' || *p == '-') p++;
            while (*p >= '0' && *p <= '9') p++;
        }
        // store integral part; keep raw text for floats
        char tmp[64];
        int n = (int)(p - start);
        if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
        for (int k = 0; k < n; k++) tmp[k] = start[k];
        tmp[n] = 0;
        out.type = JSON_INT;
        out.b = false;
        out.i = str_to_i64(tmp);
        if (is_float) out.s = tmp; // raw text preserved for float values
        return true;
    }

    bool parse_array(Value& out) {
        p++; // '['
        out = Value(); // null
        out.type = JSON_ARRAY;
        skip_ws();
        if (*p == ']') { p++; return true; }
        for (;;) {
            skip_ws();
            Value v;
            if (!parse_value(v)) return false;
            out.arr.push(v);
            skip_ws();
            if (*p == ',') { p++; continue; }
            if (*p == ']') { p++; return true; }
            return fail_at("expected ',' or ']'");
        }
    }

    bool parse_object(Value& out) {
        p++; // '{'
        out = Value(); // null
        out.type = JSON_OBJECT;
        skip_ws();
        if (*p == '}') { p++; return true; }
        for (;;) {
            skip_ws();
            if (*p != '"') return fail_at("expected string key");
            String key;
            if (!parse_string(key)) return false;
            skip_ws();
            if (*p != ':') return fail_at("expected ':'");
            p++;
            Value v;
            if (!parse_value(v)) return false;
            // duplicate keys: last wins (replace in place)
            bool replaced = false;
            for (int i = 0; i < out.obj.size(); i++) {
                if (out.obj[i].key == key) { out.obj[i].val = v; replaced = true; break; }
            }
            if (!replaced) out.obj.push(Member(key, v));
            skip_ws();
            if (*p == ',') { p++; continue; }
            if (*p == '}') { p++; return true; }
            return fail_at("expected ',' or '}'");
        }
    }
};

void escape_string(const String& in, String& out) {
    out += '"';
    for (int i = 0; i < in.len(); i++) {
        char c = in[i];
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default: out += c; break;
        }
    }
    out += '"';
}

void serialize(const Value& v, String& out, bool pretty, int depth) {
    switch (v.type) {
    case JSON_NULL: out += "null"; break;
    case JSON_BOOL: out += v.b ? "true" : "false"; break;
    case JSON_INT:  i64_to_str(v.i, out); break;
    case JSON_STRING: escape_string(v.s, out); break;
    case JSON_ARRAY: {
        out += '[';
        for (int i = 0; i < v.arr.size(); i++) {
            if (i) out += ',';
            if (pretty) { out += '\n'; for (int k = 0; k < depth + 1; k++) out += "  "; }
            serialize(v.arr[i], out, pretty, depth + 1);
        }
        if (pretty && v.arr.size()) { out += '\n'; for (int k = 0; k < depth; k++) out += "  "; }
        out += ']';
        break;
    }
    case JSON_OBJECT: {
        out += '{';
        for (int i = 0; i < v.obj.size(); i++) {
            if (i) out += ',';
            if (pretty) { out += '\n'; for (int k = 0; k < depth + 1; k++) out += "  "; }
            escape_string(v.obj[i].key, out);
            out += pretty ? ": " : ":";
            serialize(v.obj[i].val, out, pretty, depth + 1);
        }
        if (pretty && v.obj.size()) { out += '\n'; for (int k = 0; k < depth; k++) out += "  "; }
        out += '}';
        break;
    }
    }
}

} // namespace

bool parse(const char* text, Value& out, const char** err) {
    if (!text) { if (err) *err = "null input"; return false; }
    Parser ps(text);
    Value v;
    if (!ps.parse_value(v)) {
        if (err) *err = ps.err ? ps.err : "parse error";
        return false;
    }
    ps.skip_ws();
    if (*ps.p != 0) {
        if (err) *err = "trailing data after document";
        return false;
    }
    out = v;
    return true;
}

bool parse(const String& text, Value& out, const char** err) {
    return parse(text.c_str(), out, err);
}

void to_string(const Value& v, String& out, bool pretty) {
    serialize(v, out, pretty, 0);
}

String dump(const Value& v) {
    String out;
    serialize(v, out, false, 0);
    return out;
}

} // namespace json
} // namespace nefu
