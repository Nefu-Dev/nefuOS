// nefuOS 压缩算法库 —— 游程编码实现
// 见 rle.h 的接口约定。这里每个编码器都给出逐行教学注释，
// 底部自测用多种输入形状做 round-trip。
#include "rle.h"
#include "bitio.h"   // bfill/bcopy/bcmp/blen
#include <stdint.h>
#include <stdio.h>

namespace nefu {
namespace compress {

// =====================================================================
// 1) PackBits
// =====================================================================
int packbits_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n == 0) return 0;
    int o = 0;
    int i = 0;
    while (i < n) {
        // ---- 先看从 i 开始有多长的"连续相同字节" ----
        int run = 1;
        while (i + run < n && in[i + run] == in[i]) run++;

        if (run >= 3) {
            // ---- 重复段：控制字节 = -(次数-1)，后面跟 1 个重复字节 ----
            int left = run;
            while (left > 0) {
                int k = left > 128 ? 128 : left;     // PackBits 单次最多 128 次
                if (o + 2 > cap) return -1;
                out[o++] = (uint8_t)(-(k - 1));       // 有符号：0xFF..0x81
                out[o++] = in[i];
                left -= k;
            }
            i += run;
        } else {
            // ---- 字面量段：收集连续的"非长重复"字节，最多 128 个 ----
            uint8_t lit[128];
            int lc = 0;
            while (i < n && lc < 128) {
                int r = 1;
                while (i + r < n && in[i + r] == in[i]) r++;
                if (r >= 3) break;                    // 长重复段留给上面的分支
                lit[lc++] = in[i++];
            }
            // 控制字节 = lc-1（0..127），后面跟 lc 个字面量
            if (o + 1 + lc > cap) return -1;
            out[o++] = (uint8_t)(lc - 1);
            for (int t = 0; t < lc; t++) out[o++] = lit[t];
        }
    }
    return o;
}

int packbits_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        int8_t c = (int8_t)in[i++];              // 控制字节按有符号解释
        if (c >= 0) {
            int cnt = c + 1;                      // 接下来 cnt 个字面量
            if (o + cnt > cap) return -1;
            for (int k = 0; k < cnt; k++) out[o++] = in[i++];
        } else {
            int cnt = 1 - c;                      // 重复次数
            uint8_t b = in[i++];
            if (o + cnt > cap) return -1;
            for (int k = 0; k < cnt; k++) out[o++] = b;
        }
    }
    return o;
}

// =====================================================================
// 2) 通用字节 RLE（转义符 0xFB）
// =====================================================================
static const uint8_t RLE_ESC = 0xFB;

int rle_byte_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        int run = 1;
        while (i + run < n && in[i + run] == in[i]) run++;
        if (run >= 3) {
            // 重复段：ESC, len, byte（len 最大 255，超过就分多次发）
            int left = run;
            while (left > 0) {
                int k = left > 255 ? 255 : left;
                if (o + 3 > cap) return -1;
                out[o++] = RLE_ESC;
                out[o++] = (uint8_t)k;
                out[o++] = in[i];
                left -= k;
            }
            i += run;
        } else {
            uint8_t b = in[i++];
            if (b == RLE_ESC) {
                if (o + 2 > cap) return -1;
                out[o++] = RLE_ESC;
                out[o++] = 0;                     // len=0 表示"一个字面 ESC"
            } else {
                if (o + 1 > cap) return -1;
                out[o++] = b;
            }
        }
    }
    return o;
}

int rle_byte_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        uint8_t b = in[i++];
        if (b != RLE_ESC) {
            if (o + 1 > cap) return -1;
            out[o++] = b;
            continue;
        }
        // 转义：读长度
        uint8_t len = in[i++];
        if (len == 0) {
            if (o + 1 > cap) return -1;
            out[o++] = RLE_ESC;                // 字面转义符
        } else {
            uint8_t rep = in[i++];
            if (o + len > cap) return -1;
            for (int k = 0; k < len; k++) out[o++] = rep;
        }
    }
    return o;
}

// =====================================================================
// 3) 零值压缩（标记 0x9E）
// =====================================================================
static const uint8_t ZMARK = 0x9E;

int zero_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        // 统计连续 0 的个数
        int z = 0;
        while (i + z < n && in[i + z] == 0) z++;
        if (z >= 2) {
            if (o + 3 > cap) return -1;
            out[o++] = ZMARK;
            out[o++] = (uint8_t)(z & 0xFF);
            out[o++] = (uint8_t)((z >> 8) & 0xFF);
            i += z;
        } else {
            uint8_t b = in[i++];
            if (b == ZMARK) {
                if (o + 3 > cap) return -1;
                out[o++] = ZMARK; out[o++] = 0; out[o++] = 0;  // 字面标记
            } else {
                if (o + 1 > cap) return -1;
                out[o++] = b;
            }
        }
    }
    return o;
}

int zero_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        uint8_t b = in[i++];
        if (b != ZMARK) {
            if (o + 1 > cap) return -1;
            out[o++] = b;
            continue;
        }
        uint8_t lo = in[i++], hi = in[i++];
        int len = lo | (hi << 8);
        if (len == 0) {
            if (o + 1 > cap) return -1;
            out[o++] = ZMARK;
        } else {
            if (o + len > cap) return -1;
            bfill(out + o, 0, (size_t)len);
            o += len;
        }
    }
    return o;
}

// =====================================================================
// 4) 比特 RLE
// =====================================================================
int bitrle_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 5) return -1;
    uint32_t total_bits = (uint32_t)n * 8u;
    out[0] = (uint8_t)(total_bits & 0xFF);
    out[1] = (uint8_t)((total_bits >> 8) & 0xFF);
    out[2] = (uint8_t)((total_bits >> 16) & 0xFF);
    out[3] = (uint8_t)((total_bits >> 24) & 0xFF);
    int o = 4;
    if (n == 0) { out[o++] = 0; return o; }

    // 逐个比特扫描
    int cur = (in[0] >> 0) & 1;              // 首个比特
    out[o++] = (uint8_t)cur;
    uint32_t run = 1;
    for (uint32_t bi = 1; bi < total_bits; bi++) {
        int byte_idx = (int)(bi / 8u);
        int bit_idx  = (int)(bi % 8u);
        int b = (in[byte_idx] >> bit_idx) & 1;
        if (b == cur) {
            run++;
        } else {
            // 结束当前游程，切成 u16 段输出
            uint32_t left = run;
            while (left > 0) {
                uint32_t k = left > 65535u ? 65535u : left;
                if (o + 2 > cap) return -1;
                out[o++] = (uint8_t)(k & 0xFF);
                out[o++] = (uint8_t)((k >> 8) & 0xFF);
                left -= k;
            }
            cur = b; run = 1;
        }
    }
    // 收尾最后一段
    {
        uint32_t left = run;
        while (left > 0) {
            uint32_t k = left > 65535u ? 65535u : left;
            if (o + 2 > cap) return -1;
            out[o++] = (uint8_t)(k & 0xFF);
            out[o++] = (uint8_t)((k >> 8) & 0xFF);
            left -= k;
        }
    }
    return o;
}

int bitrle_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 5) return -1;
    uint32_t total_bits = (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
                          ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    uint32_t want_bytes = total_bits / 8u;
    if ((int)want_bytes > cap) return -1;
    if (total_bits == 0) return 0;

    int i = 4;
    int cur = in[i++];                        // 首个比特值在 index 4，读完 i=5
    uint32_t emitted = 0;
    bfill(out, 0, want_bytes);
    while (emitted < total_bits && i + 1 < n) {
        uint32_t len = (uint32_t)in[i] | ((uint32_t)in[i + 1] << 8);
        i += 2;
        for (uint32_t k = 0; k < len && emitted < total_bits; k++) {
            if (cur) {
                int byte_idx = (int)(emitted / 8u);
                int bit_idx  = (int)(emitted % 8u);
                out[byte_idx] |= (uint8_t)(1u << bit_idx);
            }
            emitted++;
        }
        cur ^= 1;                             // 下一段是相反的位
    }
    return (int)want_bytes;
}

// =====================================================================
// 自测
// =====================================================================
namespace {
int g_fails = 0;

// 一个 round-trip 检查：压缩 -> 解压 -> 逐字节比对
bool rt_check(const char* name,
              int (*enc)(const uint8_t*, int, uint8_t*, int),
              int (*dec)(const uint8_t*, int, uint8_t*, int),
              const uint8_t* data, int len) {
    // 给足输出空间（RLE 最坏膨胀约 +1/128；这里留 2 倍 + 64KB 余量）
    int cap = len * 2 + 65536;
    uint8_t* cbuf = new uint8_t[cap > 0 ? cap : 1];
    uint8_t* dbuf = new uint8_t[len + 64];
    int cl = enc(data, len, cbuf, cap);
    if (cl < 0) { g_fails++; delete[] cbuf; delete[] dbuf; return false; }
    int dl = dec(cbuf, cl, dbuf, len + 64);
    bool ok = (dl == len) && (len == 0 || bcmp(data, dbuf, (size_t)len) == 0);
    if (!ok) g_fails++;
    delete[] cbuf;
    delete[] dbuf;
    (void)name;
    return ok;
}

// 确定性 PRNG（LCG）
uint32_t g_seed = 0xBEEF1234;
uint32_t rnd() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
} // namespace

int rle_self_test() {
    g_fails = 0;

    // 准备多种测试数据
    const int N = 4096;
    uint8_t* data = new uint8_t[N];

    // 1) 全零
    for (int i = 0; i < N; i++) data[i] = 0;
    rt_check("zero", packbits_encode, packbits_decode, data, N);
    rt_check("zero", rle_byte_encode, rle_byte_decode, data, N);
    rt_check("zero", zero_encode, zero_decode, data, N);
    rt_check("zero", bitrle_encode, bitrle_decode, data, N);

    // 2) 全相同非零字节
    for (int i = 0; i < N; i++) data[i] = 0x55;
    rt_check("same", packbits_encode, packbits_decode, data, N);
    rt_check("same", rle_byte_encode, rle_byte_decode, data, N);
    rt_check("same", zero_encode, zero_decode, data, N);
    rt_check("same", bitrle_encode, bitrle_decode, data, N);

    // 3) 随机数据（不可压缩，主要测 round-trip 不炸）
    for (int i = 0; i < N; i++) data[i] = (uint8_t)(rnd() & 0xFF);
    rt_check("rand", packbits_encode, packbits_decode, data, N);
    rt_check("rand", rle_byte_encode, rle_byte_decode, data, N);
    rt_check("rand", zero_encode, zero_decode, data, N);
    rt_check("rand", bitrle_encode, bitrle_decode, data, N);

    // 4) 英文文本（带重复空格/单词）
    {
        const char* txt = "The quick brown fox jumps over the lazy dog.  "
                          "The quick brown fox jumps over the lazy dog.\n";
        int tl = blen(txt);
        for (int rep = 0; rep < 16; rep++)
            for (int k = 0; k < tl && rep * tl + k < N; k++)
                data[rep * tl + k] = (uint8_t)txt[k];
        int used = 16 * tl; if (used > N) used = N;
        rt_check("text", packbits_encode, packbits_decode, data, used);
        rt_check("text", rle_byte_encode, rle_byte_decode, data, used);
        rt_check("text", zero_encode, zero_decode, data, used);
        rt_check("text", bitrle_encode, bitrle_decode, data, used);
    }

    // 5) 二进制混合：穿插 0x9E / 0xFB 转义符，测转义正确性
    for (int i = 0; i < N; i++) {
        if (i % 50 == 0) data[i] = 0x9E;
        else if (i % 50 == 25) data[i] = 0xFB;
        else data[i] = (uint8_t)(rnd() & 0xFF);
    }
    rt_check("esc", packbits_encode, packbits_decode, data, N);
    rt_check("esc", rle_byte_encode, rle_byte_decode, data, N);
    rt_check("esc", zero_encode, zero_decode, data, N);
    rt_check("esc", bitrle_encode, bitrle_decode, data, N);

    // 6) 边界：空数据 / 1 字节 / 2 字节 / 128 / 129（PackBits 分块）
    rt_check("empty", packbits_encode, packbits_decode, data, 0);
    rt_check("one", packbits_encode, packbits_decode, data, 1);
    rt_check("two", packbits_encode, packbits_decode, data, 2);
    for (int i = 0; i < 129; i++) data[i] = 0x77;
    rt_check("129", packbits_encode, packbits_decode, data, 129);
    rt_check("129", rle_byte_encode, rle_byte_decode, data, 129);

    // 7) 压缩比验证：全零数据 zero_encode 应远小于原始
    for (int i = 0; i < N; i++) data[i] = 0;
    {
        uint8_t* cbuf = new uint8_t[65536];
        int cl = zero_encode(data, N, cbuf, 65536);
        // 4096 个 0 -> 这里就 3 字节；必须 < N/2
        if (cl < 0 || cl > N / 2) g_fails++;
        delete[] cbuf;
    }

    delete[] data;
    return g_fails;
}

} // namespace compress
} // namespace nefu
