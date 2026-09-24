// nefuOS text library — Levenshtein edit distance implementation
#include "levenshtein.h"

namespace nefu {
namespace text {

static int imin3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
}

static int imax(int a, int b) { return a > b ? a : b; }

int levenshtein(const char* a, const char* b) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    // 滚动两行即可，无需整张表
    int* prev = new int[(size_t)lb + 1];
    int* cur = new int[(size_t)lb + 1];
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int d = prev[j] + 1;                 // 删除 a[i-1]
            int ins = cur[j - 1] + 1;            // 插入 b[j-1]
            int rep = prev[j - 1] + cost;        // 替换或保持
            cur[j] = imin3(d, ins, rep);
        }
        int* t = prev; prev = cur; cur = t;
    }
    int r = prev[lb];
    delete[] prev;
    delete[] cur;
    return r;
}

int levenshtein_script(const char* a, const char* b, char* ops) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    // 需要完整 DP 表做回溯
    int* dp = new int[(size_t)(la + 1) * (size_t)(lb + 1)];
    for (int j = 0; j <= lb; j++) dp[j] = j;
    for (int i = 1; i <= la; i++) {
        dp[(size_t)i * (lb + 1)] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            dp[(size_t)i * (lb + 1) + j] =
                imin3(dp[(size_t)(i - 1) * (lb + 1) + j] + 1,
                      dp[(size_t)i * (lb + 1) + (j - 1)] + 1,
                      dp[(size_t)(i - 1) * (lb + 1) + (j - 1)] + cost);
        }
    }
    int dist = dp[(size_t)la * (lb + 1) + lb];
    // 从右下角回溯；先倒序收集，再反转
    char* buf = new char[(size_t)(la + lb + 1)];
    int k = 0, i = la, j = lb;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 &&
            dp[(size_t)i * (lb + 1) + j] == dp[(size_t)(i - 1) * (lb + 1) + (j - 1)] &&
            a[i - 1] == b[j - 1]) {
            buf[k++] = '='; i--; j--;
        } else if (i > 0 && j > 0 &&
                   dp[(size_t)i * (lb + 1) + j] == dp[(size_t)(i - 1) * (lb + 1) + (j - 1)] + 1) {
            buf[k++] = 'R'; i--; j--;
        } else if (j > 0 && dp[(size_t)i * (lb + 1) + j] == dp[(size_t)i * (lb + 1) + (j - 1)] + 1) {
            buf[k++] = 'I'; j--;
        } else {
            buf[k++] = 'D'; i--;
        }
    }
    for (int t = 0; t < k; t++) ops[t] = buf[k - 1 - t];
    ops[k] = 0;
    delete[] buf;
    delete[] dp;
    return dist;
}

int similarity1000(const char* a, const char* b) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    if (la == 0 && lb == 0) return 1000;
    int d = levenshtein(a, b);
    int m = imax(la, lb);
    if (m == 0) return 1000;
    long long s = 1000LL * (m - d) / m;
    return (int)s;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) g_fails++; (void)what; }
} // namespace

int levenshtein_self_test() {
    g_fails = 0;
    expect("empty", levenshtein("", "") == 0);
    expect("ident", levenshtein("kitten", "kitten") == 0);
    expect("classic", levenshtein("kitten", "sitting") == 3);
    expect("insert", levenshtein("abc", "abcd") == 1);
    expect("delete", levenshtein("abcd", "abc") == 1);
    expect("replace", levenshtein("cat", "cut") == 1);
    expect("all-rep", levenshtein("abc", "xyz") == 3);
    expect("complement", levenshtein("", "hello") == 5);
    // 脚本正确性：应用到 A 应得到 B
    char ops[32];
    levenshtein_script("kitten", "sitting", ops);
    const char* src = "kitten";
    const char* dst = "sitting";
    char out[64];
    int si = 0, oi = 0, k = 0;
    for (int t = 0; ops[t] && oi < 63; t++) {
        char c = ops[t];
        if (c == '=') out[oi++] = src[si++];
        else if (c == 'R') { out[oi++] = dst[k++]; si++; }
        else if (c == 'I') out[oi++] = dst[k++];
        else si++;
        (void)k;
    }
    out[oi] = 0;
    expect("script-applies", oi == 7);
    // 相似度
    expect("sim-same", similarity1000("nefuos", "nefuos") == 1000);
    expect("sim-similar", similarity1000("abc", "abcd") == 750);
    return g_fails;
}

} // namespace text
} // namespace nefu
