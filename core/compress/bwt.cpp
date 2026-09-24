// nefuOS 压缩算法库 —— Burrows-Wheeler 变换实现
#include "bwt.h"
#include "bitio.h"
#include <stdint.h>

namespace nefu {
namespace compress {

// =====================================================================
// 后缀数组（doubling 算法，O(n log^2 n)，教学版用简单比较排序）
// sa[i] = 第 i 小的后缀的起始位置
// =====================================================================
static void build_sa(int n, int* sa, int* rank) {
    int* tmp = new int[n];
    for (int k = 1; k < n; k <<= 1) {
        for (int a = 1; a < n; a++) {
            int v = sa[a];
            int r1v = rank[v];
            int r2v = (v + k < n) ? rank[v + k] : -1;
            int b = a - 1;
            while (b >= 0) {
                int u = sa[b];
                int r1u = rank[u];
                int r2u = (u + k < n) ? rank[u + k] : -1;
                int cmp = (r1u != r1v) ? (r1u - r1v) : (r2u - r2v);
                if (cmp <= 0) break;
                sa[b + 1] = sa[b];
                b--;
            }
            sa[b + 1] = v;
        }
        tmp[sa[0]] = 0;
        for (int i = 1; i < n; i++) {
            int u = sa[i - 1], v = sa[i];
            int diff = (rank[u] != rank[v]) ||
                       ((u + k < n ? rank[u + k] : -1) != (v + k < n ? rank[v + k] : -1));
            tmp[v] = tmp[u] + (diff ? 1 : 0);
        }
        for (int i = 0; i < n; i++) rank[i] = tmp[i];
        if (rank[sa[n - 1]] == n - 1) break;
    }
    delete[] tmp;
}

int bwt_transform(const uint8_t* in, int n, uint8_t* out, int* primary) {
    if (n <= 0) return -1;
    if (n == 1) { out[0] = in[0]; *primary = 0; return 1; }
    // 对 in+in 建后缀数组，前 n 个后缀即所有循环旋转（长度 n）
    int M = 2 * n;
    uint8_t* ext = new uint8_t[M];
    for (int i = 0; i < n; i++) { ext[i] = in[i]; ext[i + n] = in[i]; }
    int* sa = new int[M];
    int* rank = new int[M];
    for (int i = 0; i < M; i++) { sa[i] = i; rank[i] = ext[i]; }
    build_sa(M, sa, rank);
    int j = 0;
    for (int i = 0; i < M; i++) {
        if (sa[i] < n) {
            int rot = sa[i];
            int prev = rot - 1; if (prev < 0) prev += n;
            out[j] = in[prev];
            if (rot == 0) *primary = j;
            j++;
        }
    }
    delete[] ext; delete[] sa; delete[] rank;
    return n;
}

int bwt_inverse(const uint8_t* L, int n, int primary, uint8_t* out) {
    if (n <= 0) return -1;
    if (n == 1) { out[0] = L[0]; return 0; }
    // 统计每个字符出现次数，得到 F 列起始位置
    int cnt[256];
    bfill(cnt, 0, sizeof(cnt));
    for (int i = 0; i < n; i++) cnt[L[i]]++;
    int start[256];
    int acc = 0;
    for (int c = 0; c < 256; c++) { start[c] = acc; acc += cnt[c]; }
    // 预计算每一行的"同字符前缀计数"：rankrow[row] = L[row] 在 L[0..row-1] 中出现次数
    int* rankrow = new int[n];
    int seen[256];
    bfill(seen, 0, sizeof(seen));
    for (int i = 0; i < n; i++) {
        rankrow[i] = seen[L[i]];
        seen[L[i]]++;
    }
    int row = primary;
    for (int i = n - 1; i >= 0; i--) {
        out[i] = L[row];
        uint8_t ch = L[row];
        row = start[ch] + rankrow[row];
    }
    delete[] rankrow;
    return 0;
}

// =====================================================================
// Move-To-Front 变换
// =====================================================================
void mtf_encode(const uint8_t* in, int n, uint8_t* out) {
    uint8_t list[256];
    for (int i = 0; i < 256; i++) list[i] = (uint8_t)i;
    for (int i = 0; i < n; i++) {
        uint8_t c = in[i];
        // 找到 c 在表中的位置
        int pos = 0;
        while (list[pos] != c) pos++;
        out[i] = (uint8_t)pos;
        // 移到最前
        for (int j = pos; j > 0; j--) list[j] = list[j - 1];
        list[0] = c;
    }
}

void mtf_decode(const uint8_t* in, int n, uint8_t* out) {
    uint8_t list[256];
    for (int i = 0; i < 256; i++) list[i] = (uint8_t)i;
    for (int i = 0; i < n; i++) {
        int pos = in[i];
        uint8_t c = list[pos];
        out[i] = c;
        for (int j = pos; j > 0; j--) list[j] = list[j - 1];
        list[0] = c;
    }
}

// =====================================================================
// BWT+MTF 组合：头 4 字节存 n，4 字节存 primary，然后 BWT 列，再 MTF
// =====================================================================
int bwt_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 8) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) { put_u32_le(out + 4, 0); return 8; }
    uint8_t* bwt = new uint8_t[n];
    int primary = 0;
    bwt_transform(in, n, bwt, &primary);
    put_u32_le(out + 4, (uint32_t)primary);
    mtf_encode(bwt, n, out + 8);
    delete[] bwt;
    return 8 + n;
}

int bwt_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 8) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    int primary = (int)get_u32_le(in + 4);
    uint8_t* bwt = new uint8_t[orig];
    mtf_decode(in + 8, orig, bwt);
    bwt_inverse(bwt, orig, primary, out);
    delete[] bwt;
    return orig;
}

// =====================================================================
// 自测
// =====================================================================
namespace {
int g_fails = 0;
}

int bwt_self_test() {
    g_fails = 0;
    const int N = 2500;
    uint8_t* d = new uint8_t[N];
    uint8_t* c = new uint8_t[N + 16];
    uint8_t* o = new uint8_t[N];

    auto rt = [&](const char* tag, const uint8_t* data, int n) {
        int cl = bwt_encode(data, n, c, N + 16);
        if (cl < 0) { g_fails++; return; }
        int dl = bwt_decode(c, cl, o, N);
        bool ok = (dl == n);
        if (ok && n > 0) ok = bcmp(data, o, (size_t)n) == 0;
    };

    // 1) 重复文本（BWT 效果最明显）
    {
        const char* t = "abracadabra abracadabra ";
        int tl = blen(t); int k = 0;
        for (int i = 0; i < N; i++) d[i] = (uint8_t)t[k++ % tl];
        rt("rep", d, N);
    }
    // 2) 随机
    uint32_t seed = 7;
    for (int i = 0; i < N; i++) { seed = seed*1664525u+1013904223u; d[i] = (uint8_t)(seed & 0xFF); }
    rt("rep", d, N);
    // 3) 全零
    bfill(d, 0, N);
    rt("rep", d, N);
    // 4) 边界
    rt("empty", d, 0);
    d[0] = 65; rt("one", d, 1);
    d[0] = 65; d[1] = 66; rt("two", d, 2);
    // 5) MTF 单独 round-trip
    {
        uint8_t a[50], b[50], e[50];
        for (int i = 0; i < 50; i++) { seed = seed*1664525u+1013904223u; a[i] = (uint8_t)(seed & 0xFF); }
        mtf_encode(a, 50, e);
        mtf_decode(e, 50, b);
        if (bcmp(a, b, 50) != 0) g_fails++;
    }
    // 6) BWT 单独验证已知主索引（"banana" 类小例）
    {
        const char* t = "banana"; int n = 6;
        uint8_t L[16], back[16]; int prim;
        bwt_transform((const uint8_t*)t, n, L, &prim);
        bwt_inverse(L, n, prim, back);
        if (bcmp(t, back, n) != 0) g_fails++;
    }
    delete[] d; delete[] c; delete[] o;
    return g_fails;
}

} // namespace compress
} // namespace nefu
