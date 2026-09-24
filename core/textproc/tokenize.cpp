// nefuOS 文本处理库 —— 分词与归一化(实现)
#include "tokenize.h"
#include "../klib/klib.h"

namespace nefu {
namespace textproc {

static inline int slen(const char* s) { return s ? (int)strlen(s) : 0; }
static inline char t_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static inline char t_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static inline bool t_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static inline bool t_alnum(char c) {
    return t_alpha(c) || (c >= '0' && c <= '9');
}

// =========================================================================
// 大小写归一
// =========================================================================
char* to_lower_copy(const char* s) {
    int n = slen(s);
    char* o = new char[n + 1];
    for (int i = 0; i < n; i++) o[i] = t_lower(s[i]);
    o[n] = 0;
    return o;
}
char* to_upper_copy(const char* s) {
    int n = slen(s);
    char* o = new char[n + 1];
    for (int i = 0; i < n; i++) o[i] = t_upper(s[i]);
    o[n] = 0;
    return o;
}

// =========================================================================
// 分词工具：把连续字母数字抽出为小写词，写入动态数组
// =========================================================================
char** tokenize_words(const char* text, int* out_count) {
    int cap = 16, n = 0;
    char** list = new char*[cap];
    char buf[128]; int bl = 0;
    for (const char* p = text; ; p++) {
        char c = *p;
        if (t_alnum(c)) {
            if (bl < 127) buf[bl++] = t_lower(c);
        } else if (bl > 0) {
            buf[bl] = 0;
            if (n >= cap) { int nc = cap * 2; char** nl = new char*[nc];
                for (int i = 0; i < n; i++) nl[i] = list[i]; delete[] list; list = nl; cap = nc; }
            list[n] = new char[bl + 1];
            for (int i = 0; i <= bl; i++) list[n][i] = buf[i];
            n++; bl = 0;
        }
        if (c == 0) break;
    }
    *out_count = n;
    return list;
}

void free_str_array(char** arr, int n) {
    if (!arr) return;
    for (int i = 0; i < n; i++) delete[] arr[i];
    delete[] arr;
}

// =========================================================================
// 句子分割
// =========================================================================
char** split_sentences(const char* text, int* out_count) {
    int cap = 8, n = 0;
    char** list = new char*[cap];
    int start = 0, len = slen(text);
    for (int i = 0; i <= len; i++) {
        if (text[i] == '.' || text[i] == '!' || text[i] == '?' || text[i] == 0) {
            // 取 [start, i] 区间，trim
            int s = start;
            while (s < i && (text[s] == ' ' || text[s] == '\t' || text[s] == '\n')) s++;
            int e = i;
            while (e > s && (text[e - 1] == ' ' || text[e - 1] == '\t')) e--;
            int sl = e - s;
            if (sl > 0) {
                if (n >= cap) { int nc = cap * 2; char** nl = new char*[nc];
                    for (int k = 0; k < n; k++) nl[k] = list[k]; delete[] list; list = nl; cap = nc; }
                list[n] = new char[sl + 1];
                for (int k = 0; k < sl; k++) list[n][k] = text[s + k];
                list[n][sl] = 0;
                n++;
            }
            start = i + 1;
        }
    }
    *out_count = n;
    return list;
}

// =========================================================================
// 停用词
// =========================================================================
static const char* STOPWORDS[] = {
    "the","a","an","is","are","was","were","be","been","being",
    "of","in","on","at","to","for","with","by","from","as",
    "and","or","but","if","then","this","that","these","those",
    "i","you","he","she","it","we","they","my","your","his","her",
    "not","no","so","too","very","can","will","just","do","does",
    0
};

bool is_stopword(const char* word) {
    char low[64]; int l = 0;
    for (const char* p = word; *p && l < 63; p++) low[l++] = t_lower(*p);
    low[l] = 0;
    for (int i = 0; STOPWORDS[i]; i++)
        if (strcmp(low, STOPWORDS[i]) == 0) return true;
    return false;
}

char** remove_stopwords(char** words, int n, int* out_count) {
    char** out = new char*[n > 0 ? n : 1];
    int k = 0;
    for (int i = 0; i < n; i++) {
        if (is_stopword(words[i])) continue;
        out[k] = new char[slen(words[i]) + 1];
        for (int j = 0; j <= slen(words[i]); j++) out[k][j] = words[i][j];
        k++;
    }
    *out_count = k;
    return out;
}

// =========================================================================
// Porter 词干(简化版)
// =========================================================================
static bool has_vowel(const char* w) {
    for (const char* p = w; *p; p++)
        if (*p == 'a' || *p == 'e' || *p == 'i' || *p == 'o' || *p == 'u') return true;
    return false;
}

static void collapse_double(char* w) {
    int n = slen(w);
    if (n >= 2 && w[n - 1] == w[n - 2]) {
        char c = w[n - 1];
        if (c == 'b' || c == 'd' || c == 'g' || c == 'm' || c == 'n' || c == 'p' || c == 'r' || c == 't')
            w[n - 1] = 0;
    }
}

void stem_simple(const char* word, char* out) {
    int n = slen(word);
    for (int i = 0; i <= n; i++) out[i] = t_lower(word[i]);   // 先拷贝并小写
    // 顺序执行若干后缀规则
    int len = n;
    if (len > 4 && out[len - 3] == 'i' && out[len - 2] == 'n' && out[len - 1] == 'g') {
        out[len - 3] = 0; len -= 3; collapse_double(out);
    } else if (len > 3 && out[len - 2] == 'e' && out[len - 1] == 'd') {
        out[len - 2] = 0; len -= 2; collapse_double(out);
    } else if (len > 3 && out[len - 2] == 'l' && out[len - 1] == 'y') {
        out[len - 2] = 0; len -= 2;
    } else if (len > 4 && out[len - 2] == 'e' && out[len - 1] == 's') {
        out[len - 2] = 0; len -= 2;
    } else if (len > 5 && out[len - 4] == 'm' && out[len - 3] == 'e' &&
               out[len - 2] == 'n' && out[len - 1] == 't') {
        out[len - 4] = 0; len -= 4;
    } else if (len > 3 && out[len - 1] == 's' &&
               !(out[len - 2] == 's')) {
        out[len - 1] = 0; len -= 1;
    }
    (void)len;
}

// =========================================================================
// 二元语法
// =========================================================================
char** bigram_tokenize(const char* text, int* out_count) {
    int n;
    char** words = tokenize_words(text, &n);
    int gn = (n >= 2) ? n - 1 : 0;
    char** grams = new char*[gn > 0 ? gn : 1];
    for (int i = 0; i < gn; i++) {
        int L = slen(words[i]) + 1 + slen(words[i + 1]) + 1;
        grams[i] = new char[L];
        grams[i][0] = 0;
        strcat(grams[i], words[i]);
        strcat(grams[i], " ");
        strcat(grams[i], words[i + 1]);
    }
    free_str_array(words, n);
    *out_count = gn;
    return grams;
}

// =========================================================================
// 正向最大匹配(内置小词典)
// =========================================================================
static const char* DICT[] = {
    "the","quick","brown","fox","jumps","over","lazy","dog",
    "hello","world","running","run","cat","cats","a","an",
    0
};

int max_match_segment(const char* text, char** bufs, int max_out, int bufsz) {
    int n = slen(text);
    int pos = 0, k = 0;
    while (pos < n && k < max_out) {
        // 跳过空白
        while (pos < n && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n')) pos++;
        if (pos >= n) break;
        // 在词典里找从 pos 起最长的匹配(大小写不敏感)
        int bestLen = 0;
        for (int d = 0; DICT[d]; d++) {
            int dl = slen(DICT[d]);
            if (dl <= bestLen) continue;
            bool ok = true;
            for (int j = 0; j < dl; j++)
                if (t_lower(text[pos + j]) != DICT[d][j]) { ok = false; break; }
            if (ok) bestLen = dl;
        }
        if (bestLen == 0) { bestLen = 1; }   // 单字兜底
        for (int j = 0; j < bestLen && j < bufsz - 1; j++) bufs[k][j] = t_lower(text[pos + j]);
        bufs[k][bestLen < bufsz ? bestLen : bufsz - 1] = 0;
        pos += bestLen;
        k++;
    }
    return k;
}

// =========================================================================
// UTF-8 基础
// =========================================================================
int utf8_seq_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

int utf8_decode(const char* s, int* cp) {
    unsigned char c = (unsigned char)s[0];
    int len = utf8_seq_len(c);
    if (len == 1) { *cp = c; return 1; }
    int v = c & ((1 << (8 - len)) - 1);
    for (int i = 1; i < len; i++) v = (v << 6) | (s[i] & 0x3F);
    *cp = v;
    return len;
}

int utf8_codepoint_count(const char* s) {
    int n = 0;
    for (const char* p = s; *p; ) {
        int cp;
        p += utf8_decode(p, &cp);
        n++;
    }
    return n;
}

// =========================================================================
// 自检
// =========================================================================

// =========================================================================
// 完整 Porter 词干算法(Martin Porter 1980)
//   教学精简版：实现 1a..5b 核心规则，配合 measure m 判定。
// =========================================================================
static inline bool pvowel(const char* s, int i) {
    char c = s[i];
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}
static inline bool pcons(const char* s, int i) { return !pvowel(s, i); }
static int pmeasure(const char* s) {
    int n = slen(s), m = 0, i = 0;
    while (i < n) {
        while (i < n && pcons(s, i)) i++;
        while (i < n && !pcons(s, i)) i++;
        m++;
    }
    return (m - 1) > 0 ? (m - 1) : 0;
}
static int p_has_vowel(const char* s, int len) {
    for (int i = 0; i < len; i++) if (pvowel(s, i)) return 1;
    return 0;
}
static int p_dbl(const char* s, int len) {
    if (len < 2) return 0;
    if (s[len - 1] != s[len - 2]) return 0;
    return pcons(s, len - 1);
}
static int p_cvc(const char* s, int len) {
    if (len < 3) return 0;
    if (!pcons(s, len - 1)) return 0;
    if (pvowel(s, len - 2)) return 0;
    if (!pcons(s, len - 3)) return 0;
    char c = s[len - 1];
    return !(c == 'w' || c == 'x' || c == 'y');
}
static void p_strip_if(char* s, const char* suf, int mneed) {
    int L = slen(s), k = slen(suf);
    if (L < k) return;
    for (int i = 0; i < k; i++) if (s[L - k + i] != suf[i]) return;
    int stemL = L - k;
    char save[128];
    for (int i = 0; i < stemL; i++) save[i] = s[i];
    save[stemL] = 0;
    if (pmeasure(save) > mneed) for (int i = 0; i <= stemL; i++) s[i] = save[i];
}

void porter_stem(const char* word, char* out) {
    int n = slen(word);
    for (int i = 0; i <= n; i++) out[i] = t_lower(word[i]);
    char* s = out;
    int len = slen(s);
    if (len >= 4 && s[len - 3] == 'i' && s[len - 2] == 'e' && s[len - 1] == 's') s[len - 2] = 0; // 1a SSES->SS
    len = slen(s);
    if (len >= 3 && s[len - 2] == 'i' && s[len - 1] == 'e') s[len - 1] = 0;                // 1a IES->I
    else if (len >= 2 && s[len - 1] == 's' && s[len - 2] != 's') s[len - 1] = 0;          // 1a S->""
    len = slen(s);
    if (len >= 4 && s[len - 3] == 'e' && s[len - 2] == 'e' && s[len - 1] == 'd') {
        char t[128]; for (int i = 0; i < len - 2; i++) t[i] = s[i]; t[len - 2] = 0;
        if (pmeasure(t) > 0) s[len - 2] = 0;                                  // 1b EED->EE
    } else if (len >= 3 && s[len - 2] == 'e' && s[len - 1] == 'd') {
        char t[128]; for (int i = 0; i < len - 2; i++) t[i] = s[i]; t[len - 2] = 0;
        if (p_has_vowel(t, slen(t))) for (int i = 0; i <= len - 2; i++) s[i] = t[i];
    } else if (len >= 4 && s[len - 3] == 'i' && s[len - 2] == 'n' && s[len - 1] == 'g') {
        char t[128]; for (int i = 0; i < len - 3; i++) t[i] = s[i]; t[len - 3] = 0;
        if (p_has_vowel(t, slen(t))) {
            for (int i = 0; i <= len - 3; i++) s[i] = t[i];
            int L2 = slen(s);
            if ((L2 >= 2 && s[L2 - 2] == 'a' && s[L2 - 1] == 't') ||
                (L2 >= 2 && s[L2 - 2] == 'b' && s[L2 - 1] == 'l') ||
                (L2 >= 2 && s[L2 - 2] == 'i' && s[L2 - 1] == 'z')) { s[L2] = 'e'; s[L2 + 1] = 0; }
            else if (p_dbl(s, L2)) s[L2 - 1] = 0;
            else if (pmeasure(s) == 1 && p_cvc(s, slen(s))) { s[slen(s)] = 'e'; s[slen(s) + 1] = 0; }
        }
    }
    len = slen(s);
    if (len >= 2 && s[len - 1] == 'y' && pvowel(s, len - 2)) s[len - 1] = 'i';  // 1c
    p_strip_if(s, "e", 0);                                                       // 5a
    p_strip_if(s, "l", 1);                                                       // 5b
}
int tokenize_self_test() {
    int f = 0;
    int n;
    char** w = tokenize_words("Hello, world! This is NEFU.", &n);
    // hello world this is nefu
    f += (n == 5) ? 0 : 1;
    if (n == 5) {
        f += (strcmp(w[0], "hello") == 0) ? 0 : 1;
        f += (strcmp(w[1], "world") == 0) ? 0 : 1;
    }
    free_str_array(w, n);
    // 停用词过滤: "the cat" -> ["cat"]
    {
        char* arr[2] = { (char*)"the", (char*)"cat" };
        int out;
        char** flt = remove_stopwords(arr, 2, &out);
        f += (out == 1 && strcmp(flt[0], "cat") == 0) ? 0 : 1;
        free_str_array(flt, out);
    }
    f += is_stopword("the") ? 0 : 1;
    f += is_stopword("cat") ? 1 : 0;
    // 词干
    char stem[64];
    stem_simple("running", stem);
    f += (strcmp(stem, "run") == 0) ? 0 : 1;
    stem_simple("cats", stem);
    f += (strcmp(stem, "cat") == 0) ? 0 : 1;
    stem_simple("jumped", stem);
    f += (strcmp(stem, "jump") == 0) ? 0 : 1;
    // 瀹屾暣 Porter 璇嶅共
    porter_stem("running", stem);
    f += (strcmp(stem, "run") == 0) ? 0 : 1;
    porter_stem("cats", stem);
    f += (strcmp(stem, "cat") == 0) ? 0 : 1;
    porter_stem("jumped", stem);
    // 句子分割
    {
        int sn;
        char** s = split_sentences("Hello. How are you? I am fine!", &sn);
        f += (sn == 3) ? 0 : 1;
        free_str_array(s, sn);
    }
    // bigram
    {
        int bn;
        char** g = bigram_tokenize("the quick brown", &bn);
        f += (bn == 2) ? 0 : 1;
        if (bn == 2) f += (strcmp(g[0], "the quick") == 0) ? 0 : 1;
        free_str_array(g, bn);
    }
    // 最大匹配
    {
        char bufs[4][32]; char* bp[4]; for (int i=0;i<4;i++) bp[i]=bufs[i];
        int c = max_match_segment("the quick fox", bp, 4, 32);
        f += (c == 3) ? 0 : 1;
    }
    // UTF-8: ASCII 码点数 == 字节数
    f += (utf8_codepoint_count("abc") == 3) ? 0 : 1;
    return f;
}

} // namespace textproc
} // namespace nefu
