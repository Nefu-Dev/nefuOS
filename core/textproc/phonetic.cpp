// nefuOS 文本处理库 —— 语音编码(实现)
#include "phonetic.h"
#include "../klib/klib.h"

namespace nefu {
namespace textproc {

static inline int slen(const char* s) { return s ? (int)strlen(s) : 0; }
static inline char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }

// Soundex 辅音数字映射；非编码字母返回 0
static int sdx_digit(char c) {
    switch (up(c)) {
        case 'B': case 'F': case 'P': case 'V': return 1;
        case 'C': case 'G': case 'J': case 'K': case 'Q':
        case 'S': case 'X': case 'Z': return 2;
        case 'D': case 'T': return 3;
        case 'L': return 4;
        case 'M': case 'N': return 5;
        case 'R': return 6;
        default: return 0;
    }
}

void soundex(const char* name, char* out) {
    out[0] = out[1] = out[2] = out[3] = '0'; out[4] = 0;
    if (!name || !*name) return;
    // 找第一个字母
    int i = 0;
    while (name[i] && !((name[i] >= 'a' && name[i] <= 'z') ||
                        (name[i] >= 'A' && name[i] <= 'Z'))) i++;
    if (!name[i]) { out[0] = '0'; return; }
    out[0] = up(name[i]);
    int last = sdx_digit(name[i]);
    int k = 1;
    for (int j = i + 1; name[j] && k < 4; j++) {
        char c = up(name[j]);
        if (c < 'A' || c > 'Z') continue;
        if (c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' || c == 'Y') {
            last = -1;                 // 元音打断相邻合并
            continue;
        }
        if (c == 'H' || c == 'W') continue;  // h/w 不打断、也不编码
        int d = sdx_digit(c);
        if (d == 0) continue;
        if (d == last) continue;       // 相邻同码合并
        out[k++] = (char)('0' + d);
        last = d;
    }
    out[4] = 0;
}

// =========================================================================
// Metaphone(主码，教学简化版)
// =========================================================================
static bool is_vowel(char c) {
    c = up(c);
    return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

void metaphone(const char* word, char* out, int maxlen) {
    if (maxlen <= 0) return;
    out[0] = 0;
    if (!word) return;
    char w[64]; int n = 0;
    for (const char* p = word; *p && n < 63; p++)
        if (((*p) >= 'a' && (*p) <= 'z') || ((*p) >= 'A' && (*p) <= 'Z')) w[n++] = up(*p);
    w[n] = 0;
    if (n == 0) return;
    int k = 0, i = 0;
    while (i < n && k < maxlen - 1) {
        char c = w[i];
        // 开头 KN  -> 只发 N
        if (i == 0 && c == 'K' && w[1] == 'N') { i++; continue; }
        // 开头 GH -> G
        if (c == 'C' && i > 0 && w[i - 1] == 'S' && is_vowel(w[i + 1])) { i++; continue; } // SC 元音 -> S
        if (c == 'C') {
            if (w[i + 1] == 'H') { out[k++] = 'X'; i += 2; continue; }
            if (w[i + 1] == 'I' && w[i + 2] == 'A') { out[k++] = 'X'; i += 3; continue; }
            out[k++] = 'K'; i++; continue;
        }
        if (c == 'G' && w[i + 1] == 'H') { out[k++] = 'F'; i += 2; continue; }
        if (c == 'G' && w[i + 1] == 'N') { i++; continue; }
        if (c == 'P' && w[i + 1] == 'H') { out[k++] = 'F'; i += 2; continue; }
        if (c == 'T' && w[i + 1] == 'I' && w[i + 2] == 'O') { out[k++] = 'X'; i += 3; continue; }
        if (c == 'T' && w[i + 1] == 'H') { out[k++] = '0'; i += 2; continue; } // th
        if (c == 'D' && w[i + 1] == 'G') { out[k++] = 'J'; i += 2; continue; }
        if (c == 'Q' || c == 'C' || c == 'K') { out[k++] = 'K'; i++; continue; }
        if (c == 'X') { out[k++] = 'K'; out[k++] = 'S'; i++; continue; }
        if (c == 'Z') { out[k++] = 'S'; i++; continue; }
        if (c == 'W' || c == 'H') { i++; continue; }
        if (is_vowel(c)) { i++; continue; }
        out[k++] = c; i++;
    }
    out[k] = 0;
}

void double_metaphone(const char* word, char* primary, char* secondary, int maxlen) {
    // 简化：主码即 metaphone，次码相同(教学版不区分软音)
    metaphone(word, primary, maxlen);
    for (int i = 0; i < maxlen; i++) secondary[i] = primary[i];
}

// =========================================================================
// NYSIIS
// =========================================================================
void nysiis(const char* word, char* out, int maxlen) {
    if (maxlen <= 0) return;
    out[0] = 0;
    if (!word) return;
    char w[64]; int n = 0;
    for (const char* p = word; *p && n < 62; p++)
        if (((*p) >= 'a' && (*p) <= 'z') || ((*p) >= 'A' && (*p) <= 'Z')) w[n++] = up(*p);
    w[n] = 0;
    if (n == 0) return;
    // 前缀替换
    if (n >= 5 && w[0] == 'M' && w[1] == 'C') { w[0] = 'M'; }
    if (n >= 3 && w[0] == 'M' && w[1] == 'C') { w[0] = 'M'; }
    // 后缀替换 PH->FF
    for (int i = 0; i + 1 < n; i++) {
        if (w[i] == 'P' && w[i + 1] == 'H') { w[i] = 'F'; w[i + 1] = 'F'; }
    }
    // 元音折叠
    for (int i = 0; i < n; i++) {
        if (w[i] == 'E' && i == n - 1) w[i] = 0;
        else if (is_vowel(w[i])) w[i] = 'A';
    }
    // 削除连续重复
    char o[64]; int k = 0;
    for (int i = 0; i < n && w[i]; i++) {
        if (k > 0 && o[k - 1] == w[i]) continue;
        o[k++] = w[i];
    }
    o[k] = 0;
    for (int i = 0; i < k && i < maxlen - 1; i++) out[i] = o[i];
    out[k < maxlen ? k : maxlen - 1] = 0;
}

// =========================================================================
// Caverphone v1(近似)：把词归一后输出 10 位数字
// =========================================================================
void caverphone(const char* word, char* out11) {
    for (int i = 0; i < 11; i++) out11[i] = (i < 10 ? '1' : 0);  // 默认全 1
    if (!word) return;
    char w[64]; int n = 0;
    for (const char* p = word; *p && n < 62; p++)
        if (((*p) >= 'a' && (*p) <= 'z') || ((*p) >= 'A' && (*p) <= 'Z')) w[n++] = up(*p);
    w[n] = 0;
    // 去掉开头的 c/g/p 结尾不发音 e 等(简化：只做若干替换)
    for (int i = 0; i + 1 < n; i++) {
        if (w[i] == 'G' && w[i + 1] == 'H') { w[i] = '2'; w[i + 1] = '2'; }
        else if (w[i] == 'P' && w[i + 1] == 'H') { w[i] = 'F'; w[i + 1] = 'F'; }
    }
    // 数字位：统计 r/l 等出现次数作为近似(教学实现，不追求完全一致)
    int r = 0, l = 0;
    for (int i = 0; i < n; i++) {
        if (w[i] == 'R') r++;
        if (w[i] == 'L') l++;
    }
    out11[0] = '1';
    out11[1] = r ? '3' : '1';
    out11[2] = l ? '4' : '1';
    out11[10] = 0;
}

bool phonetic_match(const char* a, const char* b) {
    char sa[5], sb[5];
    soundex(a, sa);
    soundex(b, sb);
    return strcmp(sa, sb) == 0;
}

// =========================================================================
// 自检
// =========================================================================
int phonetic_self_test() {
    int f = 0;
    char buf[16];
    soundex("Robert", buf);
    f += (strcmp(buf, "R163") == 0) ? 0 : 1;
    soundex("Rupert", buf);
    f += (strcmp(buf, "R163") == 0) ? 0 : 1;
    // Smith / Smyth 同音
    f += phonetic_match("Smith", "Smyth") ? 0 : 1;
    f += phonetic_match("Robert", "Rupert") ? 0 : 1;
    // 不相等的词不应匹配
    char s1[5], s2[5];
    soundex("Smith", s1);
    soundex("Jones", s2);
    f += (strcmp(s1, s2) != 0) ? 0 : 1;
    // 各编码不崩、NUL 结尾(此处只验证可运行且不越界)
    metaphone("knight", buf, 16);
    char p[16], s[16];
    double_metaphone("hello", p, s, 16);
    nysiis("macdonald", buf, 16);
    caverphone("read", buf);
    f += (buf[10] == 0) ? 0 : 1;
    return f;
}

} // namespace textproc
} // namespace nefu
