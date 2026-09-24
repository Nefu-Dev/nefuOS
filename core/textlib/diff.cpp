// nefuOS text library — line diff implementation
// 算法：完整 LCS 回溯（n*m 表，n/m 为行数）。对行数较大的输入
// 内存会涨，但本实现面向教学与中小规模文本，清晰优先。
#include "diff.h"
#include <string.h>
#include <stdio.h>

namespace nefu {
namespace text {

int split_lines(const char* text, const char** out, int max_lines) {
    int cnt = 0;
    const char* p = text;
    while (*p && cnt < max_lines) {
        out[cnt++] = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
        // 跳过行内残留的 \r
    }
    // 忽略最后一个"空行"（文本以 \n 结尾产生的空段）
    if (cnt > 0 && *out[cnt - 1] == 0) cnt--;
    return cnt;
}

static bool line_eq(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

int line_diff(const char* const* a, int a_lines,
              const char* const* b, int b_lines,
              DiffLine* out, int out_cap) {
    if (a_lines <= 0 && b_lines <= 0) return 0;
    // dp[i][j] = LCS 长度；滚动两行不行（需回溯），用完整表
    int* dp = new int[(size_t)(a_lines + 1) * (size_t)(b_lines + 1)];
    for (int j = 0; j <= b_lines; j++) dp[j] = 0;
    for (int i = 1; i <= a_lines; i++) {
        dp[(size_t)i * (b_lines + 1)] = 0;
        for (int j = 1; j <= b_lines; j++) {
            if (line_eq(a[i - 1], b[j - 1]))
                dp[(size_t)i * (b_lines + 1) + j] = dp[(size_t)(i - 1) * (b_lines + 1) + (j - 1)] + 1;
            else {
                int u = dp[(size_t)(i - 1) * (b_lines + 1) + j];
                int l = dp[(size_t)i * (b_lines + 1) + (j - 1)];
                dp[(size_t)i * (b_lines + 1) + j] = u > l ? u : l;
            }
        }
    }
    // 回溯收集（倒序）
    DiffLine* rev = new DiffLine[(size_t)(a_lines + b_lines + 1)];
    int k = 0;
    int i = a_lines, j = b_lines;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && line_eq(a[i - 1], b[j - 1]) &&
            dp[(size_t)i * (b_lines + 1) + j] == dp[(size_t)(i - 1) * (b_lines + 1) + (j - 1)] + 1) {
            rev[k].op = DIFF_KEEP; rev[k].src_line = i - 1; rev[k].dst_line = j - 1;
            k++; i--; j--;
        } else if (i > 0 && dp[(size_t)i * (b_lines + 1) + j] ==
                   dp[(size_t)(i - 1) * (b_lines + 1) + j]) {
            // 原文件该行未进入 LCS -> 删除
            rev[k].op = DIFF_DEL; rev[k].src_line = i - 1; rev[k].dst_line = -1;
            k++; i--;
        } else {
            // 新文件该行未进入 LCS -> 新增
            rev[k].op = DIFF_ADD; rev[k].src_line = -1; rev[k].dst_line = j - 1;
            k++; j--;
        }
    }
    int cnt = 0;
    for (int t = k - 1; t >= 0 && cnt < out_cap; t--) out[cnt++] = rev[t];
    delete[] rev;
    delete[] dp;
    return cnt;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int diff_self_test() {
    g_fails = 0;
    const char* a1[] = {"line1", "line2", "line3"};
    const char* b1[] = {"line1", "line2", "line3"};
    DiffLine out[16];
    int n = line_diff(a1, 3, b1, 3, out, 16);
    expect("diff-same-count", n == 3);
    bool allkeep = true;
    for (int i = 0; i < n; i++) if (out[i].op != DIFF_KEEP) allkeep = false;
    expect("diff-same-keep", allkeep);
    // 追加一行
    const char* b2[] = {"line1", "line2", "line3", "line4"};
    n = line_diff(a1, 3, b2, 4, out, 16);
    expect("diff-append", n == 4 && out[3].op == DIFF_ADD && out[3].dst_line == 3);
    // 删除一行
    const char* b3[] = {"line1", "line3"};
    n = line_diff(a1, 3, b3, 2, out, 16);
    expect("diff-del", n == 3);
    bool sawDel = false;
    for (int i = 0; i < n; i++) if (out[i].op == DIFF_DEL) sawDel = true;
    expect("diff-del-op", sawDel);
    // 全部替换
    const char* b4[] = {"x", "y"};
    n = line_diff(a1, 3, b4, 2, out, 16);
    int dels = 0, adds = 0;
    for (int i = 0; i < n; i++) {
        if (out[i].op == DIFF_DEL) dels++;
        if (out[i].op == DIFF_ADD) adds++;
    }
    expect("diff-replace", dels == 3 && adds == 2);
    // split_lines
    const char* lines[8];
    int ln = split_lines("a\nb\nc\n", lines, 8);
    expect("diff-split", ln == 3 && lines[0][0] == 'a' && lines[2][0] == 'c');
    ln = split_lines("", lines, 8);
    expect("diff-split-empty", ln == 0);
    return g_fails;
}

} // namespace text
} // namespace nefu
