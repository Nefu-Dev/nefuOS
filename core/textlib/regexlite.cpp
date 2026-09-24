// nefuOS text library — lightweight regex implementation
// 分两阶段：①把模式编译成 token 序列（原子 + 量词标记）；②带回溯的
// 匹配循环（贪婪优先，失败回溯）。支持 . [abc] [^abc] 转义类 \d \w \s
// 及其大写取反、^ $ 锚点、* + ? 量词。不支持分组与反向引用（教学版）。
#include "regexlite.h"
#include <stdio.h>

namespace nefu {
namespace text {

namespace {

// ---- 编译产物 ----
enum TokType { TT_CHAR, TT_DOT, TT_CLASS, TT_AB, TT_AE };
enum Quant { Q_NONE = 0, Q_STAR, Q_PLUS, Q_OPT };

struct Tok {
    int type;
    int ch;              // TT_CHAR 的字符
    unsigned char cls[32]; // TT_CLASS 位图（256 位）
    bool neg;            // 类取反
    int q;               // 量词
};

const int MAX_TOK = 128;

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_wordc(char c) { return is_alpha(c) || is_digit(c) || c == '_'; }
bool is_spacec(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

void set_bit(unsigned char* cls, int c) { cls[c >> 3] |= (unsigned char)(1u << (c & 7)); }

// 编译模式到 toks；返回 token 数，失败返回 -1
int compile(const char* p, Tok* toks) {
    int nt = 0;
    int i = 0;
    while (p[i] && nt < MAX_TOK) {
        Tok& t = toks[nt];
        t.type = TT_CHAR; t.ch = 0; t.neg = false; t.q = Q_NONE;
        for (int b = 0; b < 32; b++) t.cls[b] = 0;
        char c = p[i];
        if (c == '.') { t.type = TT_DOT; i++; }
        else if (c == '^') { t.type = TT_AB; i++; }
        else if (c == '$') { t.type = TT_AE; i++; }
        else if (c == '\\') {
            char e = p[i + 1];
            if (e == 0) return -1;
            t.type = TT_CLASS; t.neg = false;
            if (e == 'd') { for (char ch = '0'; ch <= '9'; ch++) set_bit(t.cls, (unsigned char)ch); }
            else if (e == 'w') { for (char ch = '0'; ch <= '9'; ch++) set_bit(t.cls, (unsigned char)ch);
                                 for (char ch = 'a'; ch <= 'z'; ch++) set_bit(t.cls, (unsigned char)ch);
                                 for (char ch = 'A'; ch <= 'Z'; ch++) set_bit(t.cls, (unsigned char)ch);
                                 set_bit(t.cls, '_'); }
            else if (e == 's') { set_bit(t.cls, ' '); set_bit(t.cls, '\t'); set_bit(t.cls, '\n'); set_bit(t.cls, '\r'); }
            else if (e == 'D') { for (int cc = 0; cc < 256; cc++) if (!(cc >= '0' && cc <= '9')) set_bit(t.cls, cc); t.neg = true; }
            else if (e == 'W') { for (int cc = 0; cc < 256; cc++) if (!is_wordc((char)cc)) set_bit(t.cls, cc); t.neg = true; }
            else if (e == 'S') { for (int cc = 0; cc < 256; cc++) if (!is_spacec((char)cc)) set_bit(t.cls, cc); t.neg = true; }
            else { t.type = TT_CHAR; t.ch = (unsigned char)e; }
            i += 2;
        }
        else if (c == '[') {
            t.type = TT_CLASS;
            int ci = i + 1;
            if (p[ci] == '^') { t.neg = true; ci++; }
            while (p[ci] && p[ci] != ']') {
                if (p[ci + 1] == '-' && p[ci + 2] && p[ci + 2] != ']') {
                    for (int cc = (unsigned char)p[ci]; cc <= (unsigned char)p[ci + 2]; cc++)
                        set_bit(t.cls, cc);
                    ci += 3;
                } else {
                    set_bit(t.cls, (unsigned char)p[ci]);
                    ci++;
                }
            }
            if (p[ci] == ']') ci++;
            i = ci;
        }
        else { t.type = TT_CHAR; t.ch = (unsigned char)c; i++; }
        // 量词
        if (p[i] == '*') { t.q = Q_STAR; i++; }
        else if (p[i] == '+') { t.q = Q_PLUS; i++; }
        else if (p[i] == '?') { t.q = Q_OPT; i++; }
        // 锚点不带量词
        if (t.type == TT_AB || t.type == TT_AE) t.q = Q_NONE;
        nt++;
    }
    return (p[i] == 0) ? nt : -1;
}

// 匹配单个原子 token（不含量词语义）；推进 ti
bool match_atom(const Tok& t, const char* text, int& ti, int m) {
    switch (t.type) {
    case TT_DOT: if (ti < m) { ti++; return true; } return false;
    case TT_CHAR: if (ti < m && text[ti] == (char)t.ch) { ti++; return true; } return false;
    case TT_CLASS: {
        if (ti >= m) return false;
        unsigned char c = (unsigned char)text[ti];
        bool in = (t.cls[c >> 3] & (unsigned char)(1u << (c & 7))) != 0;
        if (t.neg) in = !in;
        if (!in) return false;
        ti++;
        return true;
    }
    case TT_AB: return ti == 0;
    case TT_AE: return ti == m;
    }
    return false;
}

} // namespace

// 内部匹配：full=true 整串匹配；full=false 前缀匹配（搜索用，不要求到文本尾）
static bool match_impl(const char* pattern, const char* text, bool full) {
    Tok toks[MAX_TOK];
    int nt = compile(pattern, toks);
    if (nt < 0) return false;
    int m = 0;
    while (text[m]) m++;
    // 回溯栈：{token 下标, 文本位置}
    struct F { int k, ti; };
    F stack[512];
    int sp = 0;
    int k = 0, ti = 0;
    stack[sp++] = {0, 0};
    while (sp > 0) {
        F f = stack[--sp];
        k = f.k; ti = f.ti;
        bool ok = true;
        while (k < nt) {
            const Tok& t = toks[k];
            if (t.type == TT_AB) {
                if (ti != 0) { ok = false; break; }
                k++;
                continue;
            }
            if (t.type == TT_AE) {
                if (ti != m) { ok = false; break; }
                k++;
                continue;
            }
            if (t.q == Q_NONE) {
                if (!match_atom(t, text, ti, m)) { ok = false; break; }
                k++;
                continue;
            }
            // 量词：贪婪。先数最大重复数
            int lo = (t.q == Q_PLUS) ? 1 : 0;   // '+' 至少 1 次；'*' '?' 可 0 次
            int maxr = 0;
            {
                int t2 = ti;
                while (maxr < 256) {
                    int saved = t2;
                    if (!match_atom(t, text, t2, m)) { t2 = saved; break; }
                    maxr++;
                }
            }
            // 从 maxr 递减到 lo 压栈（栈顶最后弹出 = 最小重复最后尝试）
            bool any = false;
            for (int rep = maxr; rep >= lo; rep--) {
                int t2 = ti;
                bool can = true;
                for (int r = 0; r < rep; r++) {
                    if (!match_atom(t, text, t2, m)) { can = false; break; }
                }
                if (can) { stack[sp++] = {k + 1, t2}; any = true; }
            }
            if (!any) { ok = false; break; }
            F nf = stack[--sp];
            k = nf.k; ti = nf.ti;
        }
        // full=true 必须消费完文本；full=false 只需 token 消费完
        if (ok && k >= nt && (!full || ti == m)) return true;
    }
    return false;
}

bool regex_match(const char* pattern, const char* text) {
    return match_impl(pattern, text, true);
}

int regex_search(const char* pattern, const char* text) {
    if (pattern[0] == '^') return match_impl(pattern, text, false) ? 0 : -1;
    int m = 0;
    while (text[m]) m++;
    for (int s = 0; s <= m; s++) {
        if (match_impl(pattern, text + s, false)) return s;
    }
    return -1;
}

int regex_capture(const char* pattern, const char* text, Capture* out, int cap) {
    (void)pattern; (void)text; (void)out; (void)cap;
    return 0;   // 简化实现不提供捕获组
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int regex_self_test() {
    g_fails = 0;
    expect("rx-literal", regex_match("abc", "abc"));
    expect("rx-literal-no", !regex_match("abc", "abd"));
    expect("rx-dot", regex_match("a.c", "abc"));
    expect("rx-dot2", !regex_match("a.c", "ac"));
    expect("rx-star", regex_match("ab*c", "ac"));
    expect("rx-star2", regex_match("ab*c", "abbbc"));
    expect("rx-plus", regex_match("ab+c", "abbc"));
    expect("rx-plus-no", !regex_match("ab+c", "ac"));
    expect("rx-opt", regex_match("colou?r", "color"));
    expect("rx-opt2", regex_match("colou?r", "colour"));
    expect("rx-class", regex_match("[abc]+", "cab"));
    expect("rx-class-no", !regex_match("[abc]+", "cabd"));
    expect("rx-class-neg", regex_match("[^abc]+", "xyz"));
    expect("rx-anchor-b", regex_match("^abc", "abc"));
    expect("rx-anchor-b-no", !regex_match("^abc", "xabc"));
    expect("rx-anchor-e", regex_match("abc$", "abc"));
    expect("rx-anchor-e-no", !regex_match("abc$", "abcx"));
    expect("rx-digit", regex_match("\\d+", "123"));
    expect("rx-digit-no", !regex_match("\\d+", "12a3"));
    expect("rx-word", regex_match("\\w+", "nefu_os"));
    expect("rx-email", regex_match("\\w+@\\w+\\.\\w+", "a@b.com"));
    expect("rx-empty", regex_match("", ""));
    expect("rx-any-star", regex_match(".*", "anything"));
    expect("rx-range", regex_match("[a-f]+", "deadbeef"));
    expect("rx-range-no", !regex_match("[a-f]+", "deadbeefz"));
    expect("rx-search", regex_search("\\d+", "abc123def") == 3);
    expect("rx-search-none", regex_search("\\d+", "abcdef") == -1);
    expect("rx-search-anchor", regex_search("^abc", "abcdef") == 0);
    return g_fails;
}

} // namespace text
} // namespace nefu
