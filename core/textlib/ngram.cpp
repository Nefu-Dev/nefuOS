// nefuOS text library — n-gram & statistics implementation
#include "ngram.h"
#include <string.h>
#include <stdio.h>

namespace nefu {
namespace text {

static bool is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
static bool is_cjk_lead(unsigned char c) { return c >= 0xE4 && c <= 0xE9; }
static bool is_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

void text_stats(const char* text, TextStats* st) {
    st->bytes = 0; st->chars = 0; st->words = 0; st->lines = 1;
    st->cjk_chars = 0; st->spaces = 0; st->digits = 0;
    int i = 0;
    bool in_word = false;
    while (text[i]) {
        unsigned char c = (unsigned char)text[i];
        st->bytes++;
        if (c == '\n') st->lines++;
        if (is_ws(c)) { st->spaces++; in_word = false; i++; continue; }
        if (is_cjk_lead(c)) {
            st->chars++; st->cjk_chars++; in_word = false; i += 3; continue;
        }
        if (is_alpha(c)) {
            st->chars++;
            if (!in_word) { st->words++; in_word = true; }
            i++; continue;
        }
        if (is_digit(c)) { st->chars++; st->digits++; in_word = false; i++; continue; }
        st->chars++; in_word = false; i++;
    }
    // 去掉末尾空行的额外计数
    int len = st->bytes;
    if (len > 0 && text[len - 1] == '\n') st->lines--;
}

int char_ngrams(const char* text, int n, Gram* out, int out_cap) {
    if (n <= 0) return 0;
    // 先收集所有"字符"的字节区间
    struct C { int s, e; };
    C chars[1024];
    int nc = 0;
    int i = 0;
    while (text[i] && nc < 1024) {
        unsigned char c = (unsigned char)text[i];
        if (is_cjk_lead(c)) { chars[nc].s = i; chars[nc].e = i + 3; i += 3; nc++; }
        else { chars[nc].s = i; chars[nc].e = i + 1; i++; nc++; }
    }
    int cnt = 0;
    for (int k = 0; k + n <= nc && cnt < out_cap; k++) {
        out[cnt].start = chars[k].s;
        out[cnt].end = chars[k + n - 1].e;
        cnt++;
    }
    return cnt;
}

// 简易词频：线性哈希表
namespace {
struct WEntry { char word[32]; int count; bool used; };
const int WN = 512;
unsigned hash_word(const char* s) {
    unsigned h = 5381;
    for (int i = 0; s[i]; i++) h = h * 33 + (unsigned char)s[i];
    return h;
}
} // namespace

int top_words(const char* text, int k, char (*out_words)[32], int* out_counts, int cap_words) {
    WEntry tab[WN];
    for (int i = 0; i < WN; i++) { tab[i].used = false; tab[i].count = 0; }
    // 提取单词（小写化）
    int i = 0;
    while (text[i]) {
        unsigned char c = (unsigned char)text[i];
        if (is_alpha(c)) {
            char w[32];
            int len = 0;
            while (text[i] && is_alpha((unsigned char)text[i]) && len < 31) {
                w[len++] = (text[i] >= 'A' && text[i] <= 'Z') ? text[i] + 32 : text[i];
                i++;
            }
            w[len] = 0;
            unsigned h = hash_word(w) % WN;
            // 线性探测
            while (tab[h].used && strcmp(tab[h].word, w) != 0) h = (h + 1) % WN;
            if (!tab[h].used) { for (int t = 0; t <= len; t++) tab[h].word[t] = w[t]; tab[h].used = true; }
            tab[h].count++;
        } else i++;
    }
    // 选择前 k 个（简单重复选择）
    int cnt = 0;
    while (cnt < k && cnt < cap_words) {
        int best = -1;
        for (int j = 0; j < WN; j++)
            if (tab[j].used && (best < 0 || tab[j].count > tab[best].count)) best = j;
        if (best < 0) break;
        for (int t = 0; t < 32; t++) out_words[cnt][t] = tab[best].word[t];
        out_counts[cnt] = tab[best].count;
        tab[best].used = false;
        cnt++;
    }
    return cnt;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int ngram_self_test() {
    g_fails = 0;
    TextStats st;
    // "Hello world\nSecond line" = 5+1+5+1+6+1+4 = 23 字节
    text_stats("Hello world\nSecond line", &st);
    expect("st-bytes", st.bytes == 23);
    expect("st-words", st.words == 4);
    expect("st-lines", st.lines == 2);
    text_stats("你好，世界", &st);
    expect("st-cjk", st.cjk_chars == 4);   // 中文逗号 U+FF0C 不在 E4..E9 区
    // 2-gram of "abcd" => ab, bc, cd
    Gram g[8];
    int n = char_ngrams("abcd", 2, g, 8);
    expect("ng-2", n == 3);
    expect("ng-2-first", g[0].start == 0 && g[0].end == 2);
    expect("ng-2-last", g[2].start == 2 && g[2].end == 4);
    n = char_ngrams("abc", 3, g, 8);
    expect("ng-3", n == 1);
    n = char_ngrams("abc", 4, g, 8);
    expect("ng-4", n == 0);
    // CJK n-gram
    n = char_ngrams("你好世界", 2, g, 8);
    expect("ng-cjk", n == 3);
    // top words
    char words[8][32];
    int counts[8];
    int c = top_words("the cat and the dog and the bird", 8, words, counts, 8);
    expect("tw-count", c == 5);
    expect("tw-top", strcmp(words[0], "the") == 0 && counts[0] == 3);
    expect("tw-second", strcmp(words[1], "and") == 0 && counts[1] == 2);
    return g_fails;
}

} // namespace text
} // namespace nefu
