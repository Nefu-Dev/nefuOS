// nefuOS compression library — BWT/MTF implementation
#include "bwt.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

// BWT 编码：对 n 个循环移位排序（朴素 O(n^2 log n) 教学版，n 限 1024）
int bwt_encode(const unsigned char* in, int n, unsigned char* out, int* primary) {
    if (n <= 0 || n > 1024) return -1;
    int idx[1024];
    for (int i = 0; i < n; i++) idx[i] = i;
    // 按循环移位字典序排序
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            // 比较移位 idx[i] 与 idx[j]
            int less = 0;
            for (int k = 0; k < n && !less; k++) {
                unsigned char a = in[(idx[i] + k) % n];
                unsigned char b = in[(idx[j] + k) % n];
                if (a != b) { less = (a < b) ? 1 : -1; }
            }
            if (less == -1 || (less == 0 && idx[i] > idx[j])) {
                int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
            }
        }
    }
    // 输出 L 列 + 主行
    for (int i = 0; i < n; i++) {
        out[i] = in[(idx[i] + n - 1) % n];
        if (idx[i] == 0) *primary = i;
    }
    return 0;
}

// BWT 解码：LF-mapping。L 列第 i 个字符 c，在前缀中出现次数决定
// 它在排序首列 F 的位置；从 primary 行出发逆向回溯到原串。
int bwt_decode(const unsigned char* in, int n, int primary, unsigned char* out) {
    if (n <= 0 || n > 1024 || primary < 0 || primary >= n) return -1;
    // 计数 + 累计（F 列字符序）
    int count[256], start[256];
    memset(count, 0, sizeof(count));
    for (int i = 0; i < n; i++) count[in[i]]++;
    start[0] = 0;
    for (int i = 1; i < 256; i++) start[i] = start[i - 1] + count[i - 1];
    // 构建 LF：LF[i] = start[L[i]] + (L 前缀中 L[i] 出现次数-1)
    int occur[256];
    memset(occur, 0, sizeof(occur));
    int lf[1024];
    for (int i = 0; i < n; i++) {
        unsigned char c = in[i];
        lf[i] = start[c] + occur[c];
        occur[c]++;
    }
    // 从 primary 逆向：out[n-1..0]
    int row = primary;
    for (int k = n - 1; k >= 0; k--) {
        out[k] = in[row];
        row = lf[row];
    }
    return 0;
}

int mtf_encode(const unsigned char* in, int n, unsigned char* out) {
    unsigned char table[256];
    for (int i = 0; i < 256; i++) table[i] = (unsigned char)i;
    for (int i = 0; i < n; i++) {
        unsigned char c = in[i];
        int pos = 0;
        while (table[pos] != c && pos < 256) pos++;
        if (pos == 256) return -1;
        out[i] = (unsigned char)pos;
        // 移到表头
        for (int k = pos; k > 0; k--) table[k] = table[k - 1];
        table[0] = c;
    }
    return 0;
}

int mtf_decode(const unsigned char* in, int n, unsigned char* out) {
    unsigned char table[256];
    for (int i = 0; i < 256; i++) table[i] = (unsigned char)i;
    for (int i = 0; i < n; i++) {
        int pos = in[i];
        if (pos < 0 || pos >= 256) return -1;
        unsigned char c = table[pos];
        out[i] = c;
        for (int k = pos; k > 0; k--) table[k] = table[k - 1];
        table[0] = c;
    }
    return 0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int bwt_self_test() {
    g_fails = 0;
    unsigned char L[1024], orig[1024];
    int primary = 0;
    // 经典向量：BANANA → BWT L = NNBAAA，primary = 3（标准教材例）
    {
        const char* s = "BANANA";
        int n = 6;
        bwt_encode((const unsigned char*)s, n, L, &primary);
        expect("bwt-banana-l", memcmp(L, "NNBAAA", 6) == 0);
        bwt_decode(L, n, primary, orig);
        expect("bwt-banana-roundtrip", memcmp(orig, s, 6) == 0);
    }
    // 重复文本往返
    {
        const char* s = "mississippi";
        int n = 11;
        bwt_encode((const unsigned char*)s, n, L, &primary);
        expect("bwt-miss-encode", L[0] != 0);
        bwt_decode(L, n, primary, orig);
        expect("bwt-miss-roundtrip", memcmp(orig, s, n) == 0);
    }
    // 二进制往返
    {
        unsigned char in[300];
        for (int i = 0; i < 300; i++) in[i] = (unsigned char)((i * 23 + 4) % 251);
        bwt_encode(in, 300, L, &primary);
        bwt_decode(L, 300, primary, orig);
        expect("bwt-bin-roundtrip", memcmp(orig, in, 300) == 0);
    }
    // MTF 往返
    {
        unsigned char in[200], enc[200], dec[200];
        for (int i = 0; i < 200; i++) in[i] = (unsigned char)('a' + (i / 10) % 3);   // 强聚集
        expect("mtf-encode", mtf_encode(in, 200, enc) == 0);
        expect("mtf-decode", mtf_decode(enc, 200, dec) == 0);
        expect("mtf-roundtrip", memcmp(dec, in, 200) == 0);
        // MTF 聚集性：enc 中小值多
        int small = 0;
        for (int i = 0; i < 200; i++) if (enc[i] < 4) small++;
        expect("mtf-aggregates", small > 100);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
