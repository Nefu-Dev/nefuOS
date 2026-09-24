// nefuOS 压缩算法库 —— LZ4 / Snappy 简化版实现
#include "dict.h"
#include "bitio.h"
#include <stdint.h>

namespace nefu {
namespace compress {

// =====================================================================
// LZ4 简化版
//   格式：[u32 orig_len] 随后若干 token 序列。
//   token 字节：高 4 位=字面量长度(0..15)，低 4 位=匹配长度(0..15，加4)。
//   若高 4 位==15，后接若干 u8 直到最后一个 <255。
//   字面量后接 u16 LE offset，再接匹配长度扩展（若低4位==15）。
//   末尾无 offset，以字面量结束。
// =====================================================================
#define LZ4_MINMATCH 4
#define LZ4_WIN      65536
#define LZ4_HASHSZ   4096

namespace {
// 简单哈希表：head[hash] = 最近一次出现位置
static uint32_t lz4_hash(const uint8_t* p) {
    uint32_t v = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (v * 2654435761u) >> 20;
}
}

int lz4_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4 + n + n / 2) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    int* head = new int[LZ4_HASHSZ];
    bfill(head, 0xFF, LZ4_HASHSZ * sizeof(int));
    int pos = 4, ip = 0;
    while (ip < n) {
        int lit_start = ip;
        int cand = -1;
        // 扫描字面量直到找到匹配
        while (ip < n) {
            if (ip + LZ4_MINMATCH < n) {
                uint32_t h = lz4_hash(in + ip);
                int c = head[h];
                head[h] = ip;
                if (c >= 0 && ip - c < LZ4_WIN &&
                    in[c] == in[ip] && in[c+1] == in[ip+1] &&
                    in[c+2] == in[ip+2] && in[c+3] == in[ip+3]) { cand = c; break; }
            }
            ip++;
        }
        int lit_len = ip - lit_start;
        if (ip >= n) {
            // 末尾纯字面量
            int ll = lit_len;
            out[pos++] = (uint8_t)((ll < 15 ? ll : 15) << 4);
            if (ll >= 15) { ll -= 15; while (ll >= 255) { out[pos++] = 255; ll -= 255; } out[pos++] = (uint8_t)ll; }
            for (int k = 0; k < lit_len; k++) out[pos++] = in[lit_start + k];
            break;
        }
        // 匹配长度
        int match = LZ4_MINMATCH;
        while (ip + match < n && in[cand + match] == in[ip + match]) match++;
        int offset = ip - cand;
        int ml = match - LZ4_MINMATCH;
        // 写 token：高4位=字面量长度，低4位=匹配长度
        uint8_t tok = (uint8_t)(((lit_len < 15 ? lit_len : 15) << 4) | (ml < 15 ? ml : 15));
        out[pos++] = tok;
        int ll = lit_len;
        if (ll >= 15) { ll -= 15; while (ll >= 255) { out[pos++] = 255; ll -= 255; } out[pos++] = (uint8_t)ll; }
        for (int k = 0; k < lit_len; k++) out[pos++] = in[lit_start + k];
        out[pos++] = (uint8_t)(offset & 0xFF);
        out[pos++] = (uint8_t)(offset >> 8);
        if (ml >= 15) { ml -= 15; while (ml >= 255) { out[pos++] = 255; ml -= 255; } out[pos++] = (uint8_t)ml; }
        for (int k = 0; k < match; k++)
            if (ip + k + 4 < n) { uint32_t h = lz4_hash(in + ip + k); head[h] = ip + k; }
        ip += match;
    }
    delete[] head;
    return pos;
}
int lz4_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    int p = 4, op = 0;
    while (op < orig) {
        uint8_t tok = in[p++];
        int lit_len = tok >> 4;
        int ml = (tok & 0x0F) + LZ4_MINMATCH;
        if (lit_len == 15) { int t; do { t = in[p++]; lit_len += t; } while (t == 255); }
        for (volatile int k = 0; k < lit_len; k++) { out[op] = in[p]; op++; p++; }
        if (op >= orig) break;
        int offset = in[p] | (in[p + 1] << 8); p += 2;
        if ((tok & 0x0F) == 15) { int t; do { t = in[p++]; ml += t; } while (t == 255); }
        volatile int src = op - offset;
        for (volatile int k = 0; k < ml; k++) { out[op] = out[src]; op++; src++; }
    }
    return orig;
}

// =====================================================================
// Snappy 简化版
//   token 字节：bit7=1 表示 match。
//     literal: bit7=0，低 7 位长度(0..127)，扩展用 u8。
//     match1:  11xxxxxx (len=xxxx+1, offset 11 bits：后接 u8 低3位+高8位)
//     match2:  10xxxxxx (len=xxxx+1, offset 16 bits：后接 2 u8)
// =====================================================================
int snappy_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4 + n + n / 2) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    int* head = new int[4096];
    bfill(head, 0xFF, 4096 * sizeof(int));
    int pos = 4, ip = 0;
    while (ip < n) {
        int lit_start = ip;
        int cand = -1;
        while (ip < n) {
            if (ip + 4 < n) {
                uint32_t v = ((uint32_t)in[ip]) | ((uint32_t)in[ip+1]<<8) |
                             ((uint32_t)in[ip+2]<<16) | ((uint32_t)in[ip+3]<<24);
                uint32_t h = (v * 2654435761u) >> 20;
                int c = head[h]; head[h] = ip;
                if (c >= 0 && ip - c < 65536 &&
                    in[c] == in[ip] && in[c+1] == in[ip+1] &&
                    in[c+2] == in[ip+2] && in[c+3] == in[ip+3]) { cand = c; break; }
            }
            ip++;
        }
        int lit_len = ip - lit_start;
        out[pos++] = 0x00;
        put_u32_le(out + pos, (uint32_t)lit_len); pos += 4;
        for (volatile int k = 0; k < lit_len; k++) out[pos++] = in[lit_start + k];
        if (ip >= n) break;
        int match = 4;
        while (ip + match < n && in[cand + match] == in[ip + match]) match++;
        int offset = ip - cand;
        int remaining = match;
        while (remaining > 0) {
            int chunk = remaining > 60 ? 60 : remaining;
            int mlen = chunk - 1;  // 0..63
            if (offset < 2048) {
                out[pos++] = (uint8_t)(0xC0 | (mlen & 0x3F));
                out[pos++] = (uint8_t)(offset >> 8);
                out[pos++] = (uint8_t)(offset & 0xFF);
            } else {
                out[pos++] = (uint8_t)(0x80 | (mlen & 0x3F));
                out[pos++] = (uint8_t)(offset & 0xFF);
                out[pos++] = (uint8_t)(offset >> 8);
            }
            remaining -= chunk;
        }
        ip += match;
    }
    delete[] head;
    return pos;
}

int snappy_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (cap < orig) return -1;
    int p = 4, op = 0;
    while (op < orig) {
        uint8_t b = in[p++];
        if ((b & 0x80) == 0) {
            int lit_len = (int)get_u32_le(in + p); p += 4;
            for (volatile int k = 0; k < lit_len; k++) { out[op] = in[p]; op++; p++; }
        } else if ((b & 0x40)) {
            // match1
            int mlen = (b & 0x3F) + 1;
            int offset = (in[p++] << 8) | in[p++];
            volatile int src = op - offset;
            for (volatile int k = 0; k < mlen; k++) { out[op] = out[src]; op++; src++; }
        } else {
            // match2
            int mlen = (b & 0x3F) + 1;
            int offset = in[p] | (in[p+1] << 8); p += 2;
            volatile int src = op - offset;
            for (volatile int k = 0; k < mlen; k++) { out[op] = out[src]; op++; src++; }
        }
    }
    return orig;
}

// =====================================================================
// 自测
// =====================================================================
namespace { int g_fails = 0; }

int dict_self_test() {
    g_fails = 0;
    const int N = 4000;
    uint8_t* d = new uint8_t[N];
    uint8_t* c = new uint8_t[N * 2];
    uint8_t* o = new uint8_t[N];

    auto rt = [&](const uint8_t* data, int n) {
        int cl = lz4_encode(data, n, c, N * 2);
        if (cl > 0) {
            int dl = lz4_decode(c, cl, o, N);
            bool ok = (dl == n);
            if (ok && n > 0) ok = bcmp(data, o, (size_t)n) == 0;
            if (!ok) g_fails++;
        } else g_fails++;
        int c2 = snappy_encode(data, n, c, N * 2);
        if (c2 > 0) {
            int dl = snappy_decode(c, c2, o, N);
            bool ok = (dl == n);
            if (ok && n > 0) ok = bcmp(data, o, (size_t)n) == 0;
            if (!ok) g_fails++;
        } else g_fails++;
    };

    // 重复文本
    { const char* t = "the quick brown fox jumps over the lazy dog "; int tl=44; int k=0;
      for (int i=0;i<N;i++) d[i]=(uint8_t)t[k++%tl]; rt(d,N); }
    // 随机
    uint32_t seed=9;
    for (int i=0;i<N;i++){seed=seed*1664525u+1013904223u; d[i]=(uint8_t)(seed&0xFF);}
    rt(d,N);
    // 全零
    bfill(d,0,N); rt(d,N);
    // 边界
    rt(d,0); d[0]=65; rt(d,1);
    delete[] d; delete[] c; delete[] o;
    return g_fails;
}

} // namespace compress
} // namespace nefu
