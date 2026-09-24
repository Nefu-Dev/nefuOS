// nefuOS text library — string search implementation
#include "kmp.h"
#include <stdio.h>

namespace nefu {
namespace text {

// 构造 KMP next 表：next[i] = pat[0..i] 的最长真前后缀长度
static void build_next(const char* pat, int* next, int m) {
    next[0] = 0;
    int k = 0;
    for (int i = 1; i < m; i++) {
        while (k > 0 && pat[i] != pat[k]) k = next[k - 1];
        if (pat[i] == pat[k]) k++;
        next[i] = k;
    }
}

static int plen(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

int kmp_find(const char* text, const char* pat) {
    int n = plen(text), m = plen(pat);
    if (m == 0) return 0;
    if (n < m) return -1;
    int* next = new int[(size_t)m];
    build_next(pat, next, m);
    int j = 0;
    for (int i = 0; i < n; i++) {
        while (j > 0 && text[i] != pat[j]) j = next[j - 1];
        if (text[i] == pat[j]) j++;
        if (j == m) { delete[] next; return i - m + 1; }
    }
    delete[] next;
    return -1;
}

int kmp_count(const char* text, const char* pat) {
    int n = plen(text), m = plen(pat);
    if (m == 0) return 0;
    if (n < m) return 0;
    int* next = new int[(size_t)m];
    build_next(pat, next, m);
    int cnt = 0, j = 0;
    for (int i = 0; i < n; i++) {
        while (j > 0 && text[i] != pat[j]) j = next[j - 1];
        if (text[i] == pat[j]) j++;
        if (j == m) { cnt++; j = 0; }   // 不重叠计数
    }
    delete[] next;
    return cnt;
}

int bmh_find(const char* text, const char* pat) {
    int n = plen(text), m = plen(pat);
    if (m == 0) return 0;
    if (n < m) return -1;
    // 坏字符表：字符在 pat 中最后出现的位置
    int shift[256];
    for (int c = 0; c < 256; c++) shift[c] = m;
    for (int i = 0; i < m - 1; i++) shift[(unsigned char)pat[i]] = m - 1 - i;
    int i = 0;
    while (i <= n - m) {
        int j = m - 1;
        while (j >= 0 && text[i + j] == pat[j]) j--;
        if (j < 0) return i;
        // Horspool 跳跃：总是用窗口最右字符决定移动量
        i += shift[(unsigned char)text[i + m - 1]];
    }
    return -1;
}

int naive_find(const char* text, const char* pat) {
    int n = plen(text), m = plen(pat);
    if (m == 0) return 0;
    for (int i = 0; i + m <= n; i++) {
        int j = 0;
        while (j < m && text[i + j] == pat[j]) j++;
        if (j == m) return i;
    }
    return -1;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int search_self_test() {
    g_fails = 0;
    const char* t = "ababcabcabababd";
    expect("kmp-begin", kmp_find(t, "ab") == 0);
    expect("kmp-mid", kmp_find(t, "abcab") == 2);   // "abcab" 首次出现在位置 2
    expect("kmp-end", kmp_find(t, "ababd") == 10);
    expect("kmp-none", kmp_find(t, "zzz") == -1);
    expect("kmp-whole", kmp_find("hello", "hello") == 0);
    expect("kmp-longer", kmp_find("ab", "abc") == -1);
    expect("kmp-empty-pat", kmp_find("abc", "") == 0);
    expect("kmp-count", kmp_count("aaaa", "aa") == 2);
    expect("kmp-count0", kmp_count("ababab", "aba") == 1);   // 不重叠：位置 0 之后无
    // KMP 与 BMH、朴素结果一致
    const char* texts[] = {"the quick brown fox", "banana", "a", "", "mississippi"};
    const char* pats[]  = {"quick", "ana", "a", "", "iss"};
    for (int i = 0; i < 5; i++) {
        expect("kmp-vs-naive", kmp_find(texts[i], pats[i]) == naive_find(texts[i], pats[i]));
        expect("bmh-vs-naive", bmh_find(texts[i], pats[i]) == naive_find(texts[i], pats[i]));
    }
    expect("bmh-none", bmh_find("abc", "d") == -1);
    return g_fails;
}

} // namespace text
} // namespace nefu
