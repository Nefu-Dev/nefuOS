// nefuOS text library — tokenizer implementation
#include "token.h"
#include <stdio.h>

namespace nefu {
namespace text {

void Token::copy(const char* text, char* buf) const {
    int n = end - start;
    for (int i = 0; i < n; i++) buf[i] = text[start + i];
    buf[n] = 0;
}

static bool is_word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool is_digit_char(unsigned char c) { return c >= '0' && c <= '9'; }
static bool is_ws_char(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
// CJK 常用区：U+4E00..U+9FFF（UTF-8 三字节 0xE4..0xE9 开头）
static bool is_cjk_lead(unsigned char c) { return c >= 0xE4 && c <= 0xE9; }

int tokenize(const char* text, Token* out, int out_cap) {
    int n = 0;
    int i = 0;
    while (text[i] && n < out_cap) {
        unsigned char c = (unsigned char)text[i];
        if (is_ws_char(c)) {
            int s = i;
            while (text[i] && is_ws_char((unsigned char)text[i])) i++;
            out[n].kind = TOK_WS; out[n].start = s; out[n].end = i; n++;
        } else if (is_word_char(c)) {
            int s = i;
            // 单词可含 ' 与 -（如 don't、state-of-art）
            while (text[i] && (is_word_char((unsigned char)text[i]) ||
                   ((text[i] == '\'' || text[i] == '-') && is_word_char((unsigned char)text[i + 1])))) i++;
            out[n].kind = TOK_WORD; out[n].start = s; out[n].end = i; n++;
        } else if (is_digit_char(c)) {
            int s = i;
            while (text[i] && (is_digit_char((unsigned char)text[i]) ||
                   (text[i] == '.' && is_digit_char((unsigned char)text[i + 1])))) i++;
            out[n].kind = TOK_NUM; out[n].start = s; out[n].end = i; n++;
        } else if (is_cjk_lead(c)) {
            out[n].kind = TOK_CJK; out[n].start = i;
            i += 3;   // UTF-8 三字节
            out[n].end = i; n++;
        } else {
            out[n].kind = TOK_SYM; out[n].start = i; i++;
            out[n].end = i; n++;
        }
    }
    return n;
}

void tokenize_preview(const char* text, char* buf, int buf_cap) {
    Token toks[256];
    int n = tokenize(text, toks, 256);
    int o = 0;
    for (int i = 0; i < n && o < buf_cap - 1; i++) {
        int len = toks[i].end - toks[i].start;
        for (int j = 0; j < len && o < buf_cap - 2; j++) buf[o++] = text[toks[i].start + j];
        if (i < n - 1 && o < buf_cap - 2) buf[o++] = '|';
    }
    buf[o] = 0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int token_self_test() {
    g_fails = 0;
    Token t[64];
    int n = tokenize("hello world 42, nefu-OS!", t, 64);
    // hello|space|world|space|42|,|space|nefu-OS|!  => 9 个 token
    expect("tok-count", n == 9);
    expect("tok-word", t[0].kind == TOK_WORD && t[0].end - t[0].start == 5);
    expect("tok-ws", t[1].kind == TOK_WS);
    expect("tok-num", t[4].kind == TOK_NUM);
    expect("tok-sym", t[5].kind == TOK_SYM);
    // nefu-OS 是带连字符单词（7 字符：n e f u - O S）
    expect("tok-hyphen", t[7].kind == TOK_WORD && t[7].end - t[7].start == 7);
    // CJK 分词
    n = tokenize("你好ABC", t, 64);
    expect("tok-cjk", n == 3 && t[0].kind == TOK_CJK && t[1].kind == TOK_CJK && t[2].kind == TOK_WORD);
    // 预览（空白 token 也被 '|' 分隔）
    char buf[128];
    tokenize_preview("a b", buf, 128);
    expect("tok-preview", buf[0] == 'a' && buf[1] == '|' && buf[2] == ' ' && buf[3] == '|' && buf[4] == 'b');
    // 空文本
    expect("tok-empty", tokenize("", t, 4) == 0);
    return g_fails;
}

} // namespace text
} // namespace nefu
