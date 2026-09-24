// nefuOS 文本处理库 —— 编辑距离与序列相似性(实现)
// 见 editdist.h 的 API 契约与复杂度说明。
// 本文件所有动态缓冲都用 new[]/delete[] 管理，new[] 底层路由到 kalloc。
#include "editdist.h"
#include "../klib/klib.h"   // nefu::strlen / memcpy / ksprintf

namespace nefu {
namespace textproc {

// 取字符串长度(NULL 视作空串)。这里直接用 klib 提供的 strlen。
static inline int slen(const char* s) { return s ? (int)strlen(s) : 0; }

// =========================================================================
// Levenshtein：两行滚动 DP，空间 O(min(n,m))
// =========================================================================
int levenshtein(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    if (n == 0) return m;          // 全靠插入
    if (m == 0) return n;          // 全靠删除
    // 令短的一维做列，把两行缓冲压到 m+1 个 int
    if (n < m) { const char* t = a; a = b; b = t; int k = n; n = m; m = k; }
    int* prev = new int[m + 1];
    int* curt = new int[m + 1];
    // 第 0 行：空串与 b[:j] 的距离就是 j 次插入
    for (int j = 0; j <= m; j++) prev[j] = j;
    for (int i = 1; i <= n; i++) {
        curt[0] = i;               // b 为空，a[:i] 需要 i 次删除
        char ca = a[i - 1];
        for (int j = 1; j <= m; j++) {
            int cost = (ca == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;          // 删 a[i-1]
            int ins = curt[j - 1] + 1;      // 插 b[j-1]
            int sub = prev[j - 1] + cost;   // 替换(或免费命中)
            int v = del;
            if (ins < v) v = ins;
            if (sub < v) v = sub;
            curt[j] = v;
        }
        // 交换两行
        int* tmp = prev; prev = curt; curt = tmp;
    }
    int result = prev[m];
    delete[] prev;
    delete[] curt;
    return result;
}

// =========================================================================
// Levenshtein 对齐：保留整张 DP 表，回溯出逐字符操作序列
// =========================================================================
char* levenshtein_align(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    // dp 表大小 (n+1)*(m+1)
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int j = 0; j <= m; j++) dp[j] = j;
    for (int i = 1; i <= n; i++) {
        dp[i * W] = i;
        char ca = a[i - 1];
        for (int j = 1; j <= m; j++) {
            int cost = (ca == b[j - 1]) ? 0 : 1;
            int del = dp[(i - 1) * W + j] + 1;
            int ins = dp[i * W + j - 1] + 1;
            int sub = dp[(i - 1) * W + j - 1] + cost;
            int v = del;
            if (ins < v) v = ins;
            if (sub < v) v = sub;
            dp[i * W + j] = v;
        }
    }
    // 从 (n,m) 回溯到 (0,0)，反向记录
    // 每行最多 n+m 个字符 + 1，留足余量
    int cap = n + m + 1;
    char* al = new char[cap];
    char* bl = new char[cap];
    char* op = new char[cap];
    int li = 0;
    int i = n, j = m;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && a[i - 1] == b[j - 1] &&
            dp[i * W + j] == dp[(i - 1) * W + (j - 1)]) {
            al[li] = a[i - 1]; bl[li] = b[j - 1]; op[li] = 'M'; i--; j--;
        } else if (i > 0 && j > 0 &&
                   dp[i * W + j] == dp[(i - 1) * W + (j - 1)] + 1) {
            al[li] = a[i - 1]; bl[li] = b[j - 1]; op[li] = 'S'; i--; j--;
        } else if (i > 0 && dp[i * W + j] == dp[(i - 1) * W + j] + 1) {
            al[li] = a[i - 1]; bl[li] = '-';       op[li] = 'D'; i--;
        } else {
            al[li] = '-';       bl[li] = b[j - 1]; op[li] = 'I'; j--;
        }
        li++;
    }
    // 反转三条记录
    for (int k = 0; k < li / 2; k++) {
        char t;
        t = al[k]; al[k] = al[li - 1 - k]; al[li - 1 - k] = t;
        t = bl[k]; bl[k] = bl[li - 1 - k]; bl[li - 1 - k] = t;
        t = op[k]; op[k] = op[li - 1 - k]; op[li - 1 - k] = t;
    }
    // 拼装报告: "a: ...\nb: ...\nop: ...\ndistance: N\n"
    int total = li;
    char* out = new char[total * 3 + 64];
    ksprintf(out, total * 3 + 64,
             "a: %.*s\nb: %.*s\nop: %.*s\ndistance: %d\n",
             total, al, total, bl, total, op, dp[n * W + m]);
    delete[] dp;
    delete[] al;
    delete[] bl;
    delete[] op;
    return out;
}

// =========================================================================
// Damerau-Levenshtein(最优串对齐)
//   标准递推：
//     d[i][j] = min( d[i-1][j]+1, d[i][j-1]+1,
//                    d[i-1][j-1] + (a[i-1]!=b[j-1]) )
//     若 i>=2,j>=2 且 a[i-1]==b[j-2] 且 a[i-2]==b[j-1]，
//     则再考虑 d[i-2][j-2] + 1(相邻交换)。
// =========================================================================
int damerau_levenshtein(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    if (n == 0) return m;
    if (m == 0) return n;
    int W = m + 1;
    int* d = new int[(n + 1) * W];
    for (int i = 0; i <= n; i++) d[i * W] = i;
    for (int j = 0; j <= m; j++) d[j] = j;
    for (int i = 1; i <= n; i++) {
        char ca = a[i - 1];
        for (int j = 1; j <= m; j++) {
            int cost = (ca == b[j - 1]) ? 0 : 1;
            int del = d[(i - 1) * W + j] + 1;
            int ins = d[i * W + j - 1] + 1;
            int sub = d[(i - 1) * W + j - 1] + cost;
            int v = del;
            if (ins < v) v = ins;
            if (sub < v) v = sub;
            // 相邻两字符交换: a[i-2]a[i-1] 与 b[j-2]b[j-1] 互为逆序
            if (i >= 2 && j >= 2 &&
                a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                int tr = d[(i - 2) * W + (j - 2)] + 1;
                if (tr < v) v = tr;
            }
            d[i * W + j] = v;
        }
    }
    int r = d[n * W + m];
    delete[] d;
    return r;
}

// =========================================================================
// Hamming
// =========================================================================
int hamming(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    if (n != m) return -1;         // 长度不等不可比
    int dist = 0;
    for (int i = 0; i < n; i++) if (a[i] != b[i]) dist++;
    return dist;
}

// =========================================================================
// Jaro / Jaro-Winkler
// =========================================================================
double jaro(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    if (n == 0 && m == 0) return 1.0;
    if (n == 0 || m == 0) return 0.0;
    int range = (n > m ? n : m) / 2 - 1;
    if (range < 0) range = 0;
    bool* flagA = new bool[n];
    bool* flagB = new bool[m];
    for (int zz = 0; zz < n; zz++) flagA[zz] = false;
    for (int zz = 0; zz < m; zz++) flagB[zz] = false;
    int match = 0;
    // 在窗口内双向匹配
    for (int i = 0; i < n; i++) {
        int lo = i - range; if (lo < 0) lo = 0;
        int hi = i + range; if (hi >= m) hi = m - 1;
        for (int j = lo; j <= hi; j++) {
            if (!flagB[j] && a[i] == b[j]) {
                flagA[i] = true; flagB[j] = true; match++; break;
            }
        }
    }
    if (match == 0) { delete[] flagA; delete[] flagB; return 0.0; }
    // 统计换位：把双方已匹配字符按顺序抽出，比较错位对数
    char* smA = new char[match];
    char* smB = new char[match];
    int p = 0;
    for (int i = 0; i < n; i++) if (flagA[i]) smA[p++] = a[i];
    p = 0;
    for (int j = 0; j < m; j++) if (flagB[j]) smB[p++] = b[j];
    int half = 0;
    for (int k = 0; k < match; k++) if (smA[k] != smB[k]) half++;
    double t = half / 2.0;
    double j = (match / (double)n + match / (double)m + (match - t) / match) / 3.0;
    delete[] flagA;
    delete[] flagB;
    delete[] smA;
    delete[] smB;
    return j;
}

double jaro_winkler(const char* a, const char* b) {
    double j = jaro(a, b);
    // 共同前缀长度，最多 4
    int n = slen(a), m = slen(b), l = 0;
    int lim = n < m ? n : m;
    if (lim > 4) lim = 4;
    while (l < lim && a[l] == b[l]) l++;
    double p = 0.1;               // 标准前缀权重
    return j + l * p * (1.0 - j);
}

// =========================================================================
// LCS
// =========================================================================
int lcs_length(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    if (n == 0 || m == 0) return 0;
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int zz = 0; zz < (n + 1) * W; zz++) dp[zz] = 0;
    for (int i = 1; i <= n; i++) {
        char ca = a[i - 1];
        for (int j = 1; j <= m; j++) {
            if (ca == b[j - 1]) dp[i * W + j] = dp[(i - 1) * W + (j - 1)] + 1;
            else {
                int up = dp[(i - 1) * W + j];
                int left = dp[i * W + j - 1];
                dp[i * W + j] = up > left ? up : left;
            }
        }
    }
    int r = dp[n * W + m];
    delete[] dp;
    return r;
}

char* lcs_string(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int zz = 0; zz < (n + 1) * W; zz++) dp[zz] = 0;
    for (int i = 1; i <= n; i++) {
        char ca = a[i - 1];
        for (int j = 1; j <= m; j++) {
            if (ca == b[j - 1]) dp[i * W + j] = dp[(i - 1) * W + (j - 1)] + 1;
            else {
                int up = dp[(i - 1) * W + j];
                int left = dp[i * W + j - 1];
                dp[i * W + j] = up > left ? up : left;
            }
        }
    }
    // 回溯重构
    int len = dp[n * W + m];
    char* s = new char[len + 1];
    s[len] = 0;
    int i = n, j = m, k = len;
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { s[--k] = a[i - 1]; i--; j--; }
        else if (dp[(i - 1) * W + j] >= dp[i * W + j - 1]) i--;
        else j--;
    }
    delete[] dp;
    return s;
}

// =========================================================================
// SCS：|SCS| = |a| + |b| - |LCS|，回溯时按边拼接非匹配字符
// =========================================================================
int scs_length(const char* a, const char* b) {
    return slen(a) + slen(b) - lcs_length(a, b);
}

char* scs_string(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int zz = 0; zz < (n + 1) * W; zz++) dp[zz] = 0;
    for (int i = 1; i <= n; i++) {
        char ca = a[i - 1];
        for (int j = 1; j <= m; j++) {
            if (ca == b[j - 1]) dp[i * W + j] = dp[(i - 1) * W + (j - 1)] + 1;
            else {
                int up = dp[(i - 1) * W + j];
                int left = dp[i * W + j - 1];
                dp[i * W + j] = up > left ? up : left;
            }
        }
    }
    int len = n + m - dp[n * W + m];
    char* s = new char[len + 1];
    s[len] = 0;
    int i = n, j = m, k = len;
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { s[--k] = a[i - 1]; i--; j--; }
        else if (dp[(i - 1) * W + j] > dp[i * W + j - 1]) s[--k] = a[--i];
        else s[--k] = b[--j];
    }
    while (i > 0) s[--k] = a[--i];
    while (j > 0) s[--k] = b[--j];
    delete[] dp;
    return s;
}

float similarity_ratio(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    int mx = n > m ? n : m;
    if (mx == 0) return 1.0f;
    return 1.0f - (float)levenshtein(a, b) / (float)mx;
}

// =========================================================================
// 最长公共子串(连续)
// =========================================================================
int longest_common_substring_len(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    if (n == 0 || m == 0) return 0;
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int z = 0; z < (n + 1) * W; z++) dp[z] = 0;
    int best = 0, endi = 0;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            if (a[i - 1] == b[j - 1]) {
                dp[i * W + j] = dp[(i - 1) * W + (j - 1)] + 1;
                if (dp[i * W + j] > best) { best = dp[i * W + j]; endi = i; }
            } else dp[i * W + j] = 0;
        }
    delete[] dp;
    (void)endi;
    return best;
}

char* longest_common_substring_str(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int z = 0; z < (n + 1) * W; z++) dp[z] = 0;
    int best = 0, endi = 0;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            if (a[i - 1] == b[j - 1]) {
                dp[i * W + j] = dp[(i - 1) * W + (j - 1)] + 1;
                if (dp[i * W + j] > best) { best = dp[i * W + j]; endi = i; }
            } else dp[i * W + j] = 0;
        }
    char* s = new char[best + 1];
    for (int k = 0; k < best; k++) s[k] = a[endi - best + k];
    s[best] = 0;
    delete[] dp;
    return s;
}

// =========================================================================
// Needleman-Wunsch 全局对齐打分
// =========================================================================
int needleman_wunsch_score(const char* a, const char* b,
                           int match, int mismatch, int gap) {
    int n = slen(a), m = slen(b);
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    dp[0] = 0;
    for (int j = 1; j <= m; j++) dp[j] = dp[j - 1] + gap;
    for (int i = 1; i <= n; i++) {
        dp[i * W] = dp[(i - 1) * W] + gap;
        for (int j = 1; j <= m; j++) {
            int sc = (a[i - 1] == b[j - 1]) ? match : mismatch;
            int diag = dp[(i - 1) * W + (j - 1)] + sc;
            int up = dp[(i - 1) * W + j] + gap;
            int left = dp[i * W + j - 1] + gap;
            int v = diag;
            if (up > v) v = up;
            if (left > v) v = left;
            dp[i * W + j] = v;
        }
    }
    int r = dp[n * W + m];
    delete[] dp;
    return r;
}

// =========================================================================
// Smith-Waterman 局部对齐
// =========================================================================
int smith_waterman_score(const char* a, const char* b) {
    int n = slen(a), m = slen(b);
    int W = m + 1;
    int* dp = new int[(n + 1) * W];
    for (int z = 0; z < (n + 1) * W; z++) dp[z] = 0;
    int best = 0;
    for (int i = 1; i <= n; i++)
        for (int j = 1; j <= m; j++) {
            int sc = (a[i - 1] == b[j - 1]) ? 2 : -1;
            int diag = dp[(i - 1) * W + (j - 1)] + sc;
            int up = dp[(i - 1) * W + j] - 1;
            int left = dp[i * W + j - 1] - 1;
            int v = 0;                       // 局部对齐允许从 0 重启
            if (diag > v) v = diag;
            if (up > v) v = up;
            if (left > v) v = left;
            dp[i * W + j] = v;
            if (v > best) best = v;
        }
    delete[] dp;
    return best;
}

// =========================================================================
// 词级 Jaccard / Dice(本地切词，避免依赖 tokenize 模块)
// =========================================================================
static int tp_uniq_words(const char* text, char** out, int max) {
    int n = 0;
    char buf[64]; int bl = 0;
    for (const char* p = text; ; p++) {
        char c = *p;
        bool isw = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        if (isw) {
            if (bl < 63) buf[bl++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        } else if (bl > 0) {
            buf[bl] = 0;
            bool dup = false;
            for (int k = 0; k < n; k++) if (strcmp(out[k], buf) == 0) { dup = true; break; }
            if (!dup && n < max) {
                for (int k = 0; k <= bl; k++) out[n][k] = buf[k];
                n++;
            }
            bl = 0;
        }
        if (c == 0) break;
    }
    return n;
}

double jaccard_words(const char* a, const char* b) {
    char wa[64][32], wb[64][32];
    for (int i = 0; i < 64; i++) { wa[i][0] = 0; wb[i][0] = 0; }
    char* pa[64]; char* pb[64];
    for (int i = 0; i < 64; i++) { pa[i] = wa[i]; pb[i] = wb[i]; }
    int na = tp_uniq_words(a, pa, 64);
    int nb = tp_uniq_words(b, pb, 64);
    if (na + nb == 0) return 1.0;
    int inter = 0;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            if (strcmp(wa[i], wb[j]) == 0) { inter++; break; }
    return (double)inter / (na + nb - inter);
}

double dice_words(const char* a, const char* b) {
    char wa[64][32], wb[64][32];
    for (int i = 0; i < 64; i++) { wa[i][0] = 0; wb[i][0] = 0; }
    char* pa[64]; char* pb[64];
    for (int i = 0; i < 64; i++) { pa[i] = wa[i]; pb[i] = wb[i]; }
    int na = tp_uniq_words(a, pa, 64);
    int nb = tp_uniq_words(b, pb, 64);
    if (na + nb == 0) return 1.0;
    int inter = 0;
    for (int i = 0; i < na; i++)
        for (int j = 0; j < nb; j++)
            if (strcmp(wa[i], wb[j]) == 0) { inter++; break; }
    return 2.0 * inter / (na + nb);
}

// =========================================================================
// 自检
// =========================================================================
static int check(bool cond, int fails) { return fails + (cond ? 0 : 1); }

int editdist_self_test() {
    int f = 0;
    // Levenshtein 教科书值
    f = check(levenshtein("kitten", "sitting") == 3, f);
    f = check(levenshtein("", "abc") == 3, f);
    f = check(levenshtein("abc", "") == 3, f);
    f = check(levenshtein("abc", "abc") == 0, f);
    f = check(levenshtein("saturday", "sunday") == 3, f);
    // Damerau：相邻交换只算 1
    f = check(damerau_levenshtein("abc", "bca") == 2, f);
    f = check(damerau_levenshtein("abc", "acb") == 1, f);
    f = check(damerau_levenshtein("kitten", "sitting") == 3, f);
    // Hamming
    f = check(hamming("karolin", "kathrin") == 3, f);
    f = check(hamming("1011101", "1001001") == 2, f);
    f = check(hamming("abc", "abcd") == -1, f);   // 长度不等
    // Jaro-Winkler 已知值 ≈ 0.9611
    double jw = jaro_winkler("MARTHA", "MARHTA");
    f = check(jw > 0.960 && jw < 0.963, f);
    f = check(jaro("", "") == 1.0, f);
    f = check(jaro("abc", "") == 0.0, f);
    // LCS
    f = check(lcs_length("ABCBDAB", "BDCAB") == 4, f);
    char* l = lcs_string("ABCBDAB", "BDCAB");
    f = check(l && strcmp(l, "BCAB") == 0, f);
    delete[] l;
    f = check(lcs_length("", "abc") == 0, f);
    // SCS: |a|+|b|-|lcs| = 7+5-4 = 8
    f = check(scs_length("ABCBDAB", "BDCAB") == 8, f);
    char* scs = scs_string("ABCBDAB", "BDCAB");
    f = check(scs && slen(scs) == 8, f);
    delete[] scs;
    // 对齐报告能生成且非空
    char* al = levenshtein_align("kitten", "sitting");
    f = check(al && slen(al) > 0, f);
    delete[] al;
    // 最长公共子串
    f = check(longest_common_substring_len("abcde", "cde") == 3, f);
    char* lcs2 = longest_common_substring_str("abcxyz", "xyzabc");
    f = check(lcs2 && slen(lcs2) == 3, f);
    delete[] lcs2;
    // Needleman-Wunsch 相同串打满分
    f = check(needleman_wunsch_score("abc", "abc", 2, -1, -2) == 6, f);
    // Smith-Waterman：局部片段 "abc" 得 2*3=6
    f = check(smith_waterman_score("xabcx", "abc") == 6, f);
    // Jaccard：{the,cat} 与 {the,dog} -> 交集1/并集3 = 1/3
    double j = jaccard_words("the cat", "the dog");
    f = check(j > 0.3 && j < 0.4, f);
    f = check(dice_words("a b", "a b") == 1.0, f);
    return f;
}

} // namespace textproc
} // namespace nefu
