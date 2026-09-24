// nefuOS text library — LCS implementation
#include "lcs.h"

namespace nefu {
namespace text {

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

int lcs_len(const char* a, const char* b) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    int* prev = new int[(size_t)lb + 1];
    int* cur = new int[(size_t)lb + 1];
    for (int j = 0; j <= lb; j++) prev[j] = 0;
    for (int i = 1; i <= la; i++) {
        cur[0] = 0;
        for (int j = 1; j <= lb; j++) {
            if (a[i - 1] == b[j - 1]) cur[j] = prev[j - 1] + 1;
            else cur[j] = imax(prev[j], cur[j - 1]);
        }
        int* t = prev; prev = cur; cur = t;
    }
    int r = prev[lb];
    delete[] prev;
    delete[] cur;
    return r;
}

int lcs_get(const char* a, const char* b, char* out) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    int* dp = new int[(size_t)(la + 1) * (size_t)(lb + 1)];
    for (int j = 0; j <= lb; j++) dp[j] = 0;
    for (int i = 1; i <= la; i++) {
        dp[(size_t)i * (lb + 1)] = 0;
        for (int j = 1; j <= lb; j++) {
            if (a[i - 1] == b[j - 1]) dp[(size_t)i * (lb + 1) + j] = dp[(size_t)(i - 1) * (lb + 1) + (j - 1)] + 1;
            else dp[(size_t)i * (lb + 1) + j] = imax(dp[(size_t)(i - 1) * (lb + 1) + j], dp[(size_t)i * (lb + 1) + (j - 1)]);
        }
    }
    int len = dp[(size_t)la * (lb + 1) + lb];
    // 回溯收集（倒序）
    char* tmp = new char[(size_t)len + 1];
    int k = 0, i = la, j = lb;
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) { tmp[k++] = a[i - 1]; i--; j--; }
        else if (dp[(size_t)(i - 1) * (lb + 1) + j] >= dp[(size_t)i * (lb + 1) + (j - 1)]) i--;
        else j--;
    }
    for (int t = 0; t < k; t++) out[t] = tmp[k - 1 - t];
    out[k] = 0;
    delete[] tmp;
    delete[] dp;
    return len;
}

int lcs_similarity1000(const char* a, const char* b) {
    int la = 0, lb = 0;
    while (a[la]) la++;
    while (b[lb]) lb++;
    if (la == 0 && lb == 0) return 1000;
    int m = imax(la, lb);
    if (m == 0) return 1000;
    int l = lcs_len(a, b);
    return (int)(1000LL * l / m);
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) g_fails++; (void)what; }
} // namespace

int lcs_self_test() {
    g_fails = 0;
    expect("lcs-empty", lcs_len("", "") == 0);
    expect("lcs-same", lcs_len("abcdef", "abcdef") == 6);
    expect("lcs-classic", lcs_len("ABCBDAB", "BDCABA") == 4);
    expect("lcs-none", lcs_len("abc", "xyz") == 0);
    expect("lcs-substr", lcs_len("xyzabc", "abc") == 3);
    char out[16];
    lcs_get("ABCBDAB", "BDCABA", out);
    expect("lcs-get-len", out[0] != 0);
    expect("lcs-get-len2", lcs_get("abc", "abc", out) == 3 && out[0] == 'a' && out[2] == 'c');
    expect("lcs-sim", lcs_similarity1000("abc", "abc") == 1000);
    expect("lcs-sim-half", lcs_similarity1000("abcd", "abef") == 500);
    return g_fails;
}

} // namespace text
} // namespace nefu
