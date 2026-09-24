// nefuOS 压缩算法库 —— Huffman 编码实现
// 见 huffman.h。这里实现：
//   - 由频率表建 Huffman 树 -> 每个符号的码长
//   - 由码长生成规范码字（canonical code）
//   - 静态 Huffman（带 256 字节码长头）
//   - 自适应 Huffman（NYT 模型，双方同步重建树）
#include "huffman.h"
#include "bitio.h"
#include <stdint.h>

namespace nefu {
namespace compress {

// ---------------------------------------------------------------------
// 内部工具：由频率表生成码长（Huffman 树）
//   freq[0..nsym-1]：每个符号的出现次数（0 表示未出现）
//   lengths[0..nsym-1]：输出每个符号的码长（0 表示未用）
//   返回实际出现的符号个数
// ---------------------------------------------------------------------
static int huff_build_lengths(const int* freq, int nsym, int* lengths) {
    // 节点结构：叶子带符号号，内部节点为合并产物
    const int MAXN = 600;
    int   w[MAXN];      // 权重
    int   lc[MAXN], rc[MAXN];
    int   sym[MAXN];    // >=0 表示叶子符号
    int   used[MAXN];
    int   n = 0;
    for (int s = 0; s < nsym; s++) {
        lengths[s] = 0;
        if (freq[s] > 0) {
            w[n] = freq[s]; lc[n] = rc[n] = -1; sym[n] = s; used[n] = 1;
            n++;
        }
    }
    if (n == 0) return 0;
    if (n == 1) {
        // 只有一种符号：给它长度 1（前缀码必须非空前缀）
        lengths[sym[0]] = 1;
        return 1;
    }
    int root = -1;
    // 反复取两个最小权重合并（O(n^2)，n<=257 足够快）
    while (true) {
        // 找两个未使用的最小
        int m1 = -1, m2 = -1;
        for (int i = 0; i < n; i++) {
            if (!used[i]) continue;
            if (m1 < 0 || w[i] < w[m1]) { m2 = m1; m1 = i; }
            else if (m2 < 0 || w[i] < w[m2]) m2 = i;
        }
        if (m2 < 0) { root = m1; break; }
        used[m1] = used[m2] = 0;
        int node = n++;
        w[node] = w[m1] + w[m2];
        lc[node] = m1; rc[node] = m2; sym[node] = -1; used[node] = 1;
    }
    // DFS 求每个叶子深度
    // 用显式栈避免递归
    struct Fr { int node; int depth; };
    Fr stk[600]; int sp = 0;
    stk[sp++] = {root, 0};
    int maxlen = 0;
    while (sp > 0) {
        Fr f = stk[--sp];
        if (sym[f.node] >= 0) {
            lengths[sym[f.node]] = f.depth;
            if (f.depth > maxlen) maxlen = f.depth;
        } else {
            stk[sp++] = {lc[f.node], f.depth + 1};
            stk[sp++] = {rc[f.node], f.depth + 1};
        }
    }
    // 安全：码长不能超过 32 位（我们的码字是 uint32）。
    // 若病态频率导致超长，统一压成等长码，仍保证前缀码正确。
    if (maxlen > 32) {
        int cnt = 0;
        for (int s = 0; s < nsym; s++) if (freq[s] > 0) cnt++;
        int eq = 1; while ((1 << eq) < cnt) eq++;
        for (int s = 0; s < nsym; s++) if (freq[s] > 0) lengths[s] = eq;
    }
    return n;
}

// ---------------------------------------------------------------------
// 由码长生成规范码字
//   lengths[s] : 码长
//   code_out[s] : 输出码字值（按 LSB 对齐，写入位流时从低位起）
// ---------------------------------------------------------------------
static void huff_build_canonical(const int* lengths, int nsym, uint32_t* code_out) {
    // 按 (长度, 符号) 计数排序
    int maxlen = 0;
    for (int s = 0; s < nsym; s++) if (lengths[s] > maxlen) maxlen = lengths[s];
    // 先统计每个长度有多少符号
    int* bl_count = new int[maxlen + 1];
    bfill(bl_count, 0, (size_t)(maxlen + 1) * sizeof(int));
    for (int s = 0; s < nsym; s++) if (lengths[s] > 0) bl_count[lengths[s]]++;
    // 计算每个长度的起始码字（RFC 1951 canonical 公式）
    uint32_t next_code[33];
    uint32_t code = 0;
    bl_count[0] = 0;
    for (int len = 1; len <= maxlen; len++) {
        code = (code + (uint32_t)bl_count[len - 1]) << 1;
        next_code[len] = code;
    }
    // 按长度从小到大、符号号从小到大分配
    for (int len = 1; len <= maxlen; len++) {
        for (int s = 0; s < nsym; s++) {
            if (lengths[s] == len) {
                code_out[s] = next_code[len]++;
            }
        }
    }
    delete[] bl_count;
}

// =====================================================================
// 静态 Huffman
// =====================================================================
// 把规范码字按 MSB 先发写入位流（与解码器逐位建树一致）
static void write_code(BitSink& w, uint32_t code, int len) {
    for (int b = len - 1; b >= 0; b--) w.put_bit((int)((code >> b) & 1));
}

int huffman_static_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n == 0) {
        if (cap < 2) return -1;
        put_u16_le(out, 0);          // 0 长度
        return 2;
    }
    // 头部：u16 原始长度 + 256 字节码长
    if (cap < 2 + 256) return -1;
    put_u16_le(out, (uint16_t)n);
    int freq[256];
    bfill(freq, 0, sizeof(freq));
    for (int i = 0; i < n; i++) freq[in[i]]++;
    int lengths[256];
    huff_build_lengths(freq, 256, lengths);
    for (int i = 0; i < 256; i++) out[2 + i] = (uint8_t)lengths[i];

    uint32_t code[256];
    huff_build_canonical(lengths, 256, code);

    BitSink w(out + 2 + 256, cap - 2 - 256);
    for (int i = 0; i < n; i++) {
        int s = in[i];
        write_code(w, code[s], lengths[s]);
    }
    int bits = w.finish();
    return 2 + 256 + bits;
}

int huffman_static_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 2) return -1;
    int orig = get_u16_le(in);
    if (orig == 0) return 0;
    if (n < 2 + 256) return -1;
    int lengths[256];
    for (int i = 0; i < 256; i++) lengths[i] = in[2 + i];

    // 建解码树：root=0，左=0，右=1（位流 LSB 先读，无所谓左右对称）
    // 节点数最多 2*256
    int lc[600], rc[600], leafsym[600];
    int nn = 1;
    for (int i = 0; i < 600; i++) { lc[i] = rc[i] = -1; leafsym[i] = -1; }
    // 为每个符号按其码字逐位建树
    uint32_t code[256];
    huff_build_canonical(lengths, 256, code);
    for (int s = 0; s < 256; s++) {
        if (lengths[s] == 0) continue;
        int node = 0;
        for (int b = lengths[s] - 1; b >= 0; b--) {
            int bit = (code[s] >> b) & 1;
            if (bit == 0) {
                if (lc[node] < 0) { lc[node] = nn++; }
                node = lc[node];
            } else {
                if (rc[node] < 0) { rc[node] = nn++; }
                node = rc[node];
            }
        }
        leafsym[node] = s;
    }

    BitSource r(in + 2 + 256, n - 2 - 256);
    int o = 0;
    int node = 0;
    for (int i = 0; i < orig; i++) {
        // 从根走到叶子
        while (leafsym[node] < 0) {
            int bit = r.read_bit();
            node = bit ? rc[node] : lc[node];
            if (node < 0) return -1;
        }
        if (o >= cap) return -1;
        out[o++] = (uint8_t)leafsym[node];
        node = 0;
    }
    return o;
}

// =====================================================================
// 自适应 Huffman（NYT 模型）
// 符号空间：0..255 为字节，256 为 NYT
// =====================================================================
static const int NYT = 256;
static const int NSYM = 257;

// 由当前频率重建码表（收发双方调用得到一致结果）
static void adaptive_rebuild(const int* freq, uint32_t* code, int* len) {
    int lengths[NSYM];
    huff_build_lengths(freq, NSYM, lengths);
    huff_build_canonical(lengths, NSYM, code);
    for (int i = 0; i < NSYM; i++) len[i] = lengths[i];
}

int huffman_adaptive_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4) return -1;
    put_u32_le(out, (uint32_t)n);
    int freq[NSYM];
    bfill(freq, 0, sizeof(freq));
    freq[NYT] = 1;                     // 初始只有 NYT 一个节点
    uint32_t code[NSYM]; int len[NSYM];
    adaptive_rebuild(freq, code, len);

    BitSink w(out + 4, cap - 4);
    for (int i = 0; i < n; i++) {
        int s = in[i];
        if (freq[s] == 0) {
            // 新符号：先发 NYT 码字，再发 8 位原始字节
            write_code(w, code[NYT], len[NYT]);
            w.write_bits((uint32_t)s, 8);
        } else {
            write_code(w, code[s], len[s]);
        }
        freq[s]++;
        adaptive_rebuild(freq, code, len);
    }
    int bits = w.finish();
    return 4 + bits;
}

int huffman_adaptive_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    int freq[NSYM];
    bfill(freq, 0, sizeof(freq));
    freq[NYT] = 1;
    uint32_t code[NSYM]; int len[NSYM];
    adaptive_rebuild(freq, code, len);

    // 解码需要按"当前码表"逐位读。由于码表每个符号都变，最简单：
    // 每读一位，判断当前读到的位串是否等于某个符号的当前码字。
    BitSource r(in + 4, n - 4);
    int o = 0;
    while (o < orig) {
        // 逐位读，累积前缀 p，长度 l
        uint32_t p = 0; int l = 0;
        int sym = -1;
        while (true) {
            int bit = r.read_bit();
            p = (p << 1) | (uint32_t)bit;
            l++;
            // 在当前码表里找长度==l 且码字==p 的符号
            for (int s = 0; s < NSYM; s++) {
                if (len[s] == l && code[s] == p) { sym = s; break; }
            }
            if (sym >= 0) break;
        }
        if (sym == NYT) {
            // 读 8 位原始字节
            uint32_t b = r.read_bits(8);
            if (o >= cap) return -1;
            out[o++] = (uint8_t)b;
            freq[b]++;
        } else {
            if (o >= cap) return -1;
            out[o++] = (uint8_t)sym;
            freq[sym]++;
        }
        adaptive_rebuild(freq, code, len);
    }
    return o;
}

// =====================================================================
// 自测
// =====================================================================
namespace {
int g_fails = 0;

bool rt(const char* nm,
        int (*enc)(const uint8_t*, int, uint8_t*, int),
        int (*dec)(const uint8_t*, int, uint8_t*, int),
        const uint8_t* d, int n) {
    int cap = n * 3 + 4096;
    uint8_t* c = new uint8_t[cap > 0 ? cap : 1];
    uint8_t* dd = new uint8_t[n + 64];
    int cl = enc(d, n, c, cap);
    if (cl < 0) { g_fails++; delete[] c; delete[] dd; return false; }
    int dl = dec(c, cl, dd, n + 64);
    bool ok = (dl == n) && (n == 0 || bcmp(d, dd, (size_t)n) == 0);
    if (!ok) g_fails++;
    delete[] c; delete[] dd;
    (void)nm;
    return ok;
}
uint32_t seed = 0xABCD;
uint32_t rnd() { seed = seed * 1664525u + 1013904223u; return seed; }
} // namespace

int huffman_self_test() {
    g_fails = 0;
    const int N = 2000;
    uint8_t* d = new uint8_t[N];

    // 全零（极不均匀，压缩比高）
    bfill(d, 0, N);
    rt("s", huffman_static_encode, huffman_static_decode, d, N);
    rt("a", huffman_adaptive_encode, huffman_adaptive_decode, d, N);

    // 随机近均匀（接近熵，几乎不压缩，但 round-trip 必须对）
    for (int i = 0; i < N; i++) d[i] = (uint8_t)(rnd() & 0xFF);
    rt("s", huffman_static_encode, huffman_static_decode, d, N);
    rt("a", huffman_adaptive_encode, huffman_adaptive_decode, d, N);

    // 英文文本（不均匀）
    {
        const char* t = "aaaa bbb cccc dd eeeee ffffff ggggggg ";
        int tl = blen(t); int k = 0;
        for (int i = 0; i < N; i++) d[i] = (uint8_t)t[k++ % tl];
        rt("s", huffman_static_encode, huffman_static_decode, d, N);
        rt("a", huffman_adaptive_encode, huffman_adaptive_decode, d, N);
    }

    // 边界
    rt("s", huffman_static_encode, huffman_static_decode, d, 0);
    rt("a", huffman_adaptive_encode, huffman_adaptive_decode, d, 0);
    d[0] = 42; rt("s", huffman_static_encode, huffman_static_decode, d, 1);
    rt("a", huffman_adaptive_encode, huffman_adaptive_decode, d, 1);

    // 压缩比：文本应明显变小
    {
        const char* t = "aaaa bbb cccc dd eeeee ffffff ggggggg ";
        int tl = blen(t); int k = 0;
        for (int i = 0; i < N; i++) d[i] = (uint8_t)t[k++ % tl];
        uint8_t* c = new uint8_t[N * 3];
        int cl = huffman_static_encode(d, N, c, N * 3);
        if (cl < 0 || cl > N / 2) g_fails++;     // 高度重复应压到一半以下
        delete[] c;
    }
    delete[] d;
    return g_fails;
}

} // namespace compress
} // namespace nefu
