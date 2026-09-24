// nefuOS compression library — 算术编码 implementation
#include "arithmetic.h"
#include "bitio.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

namespace {
// 教学版用 24 位区间精度：r 翻倍最多到 2^24，不溢出
const uint32_t TOP = 0xFFFFFFu;        // 2^24-1 全区间
const uint32_t HALF = 0x800000u;       // 2^23  (0.5)
const uint32_t Q1   = 0x400000u;       // 0.25
const uint32_t Q3   = 0xC00000u;       // 0.75
const int CODEBITS = 24;
}

int arithmetic_encode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    if (n == 0) return 0;
    // 频率表
    unsigned freq[256];
    memset(freq, 0, sizeof(freq));
    for (int i = 0; i < n; i++) freq[in[i]]++;
    // 累计表（含哨兵 0）
    unsigned cum[257];
    cum[0] = 0;
    unsigned total = 0;
    for (int i = 0; i < 256; i++) { total += freq[i]; cum[i + 1] = total; }
    if (total == 0) total = 1;
    // 写表头：频率（2 字节大端，教学版上限 65535）
    int hdr = 0;
    for (int i = 0; i < 256; i++) {
        unsigned f = freq[i];
        if (f > 0xFFFF) f = 0xFFFF;
        out[hdr++] = (unsigned char)(f >> 8);
        out[hdr++] = (unsigned char)(f & 0xFF);
    }
    if (hdr + 8 > outcap) return -1;
    BitWriter w(out + hdr, outcap - hdr);
    // 区间编码：低端点 low，区间宽 r（Witten 经典实现）
    uint32_t low = 0;
    uint32_t r = TOP;
    int pending = 0;              // E2 延迟位计数
    // 输出当前位，并补 pending 个反位（E2 延迟机制）
    auto out_bit = [&](int b) {
        w.write_bit(b);
        while (pending > 0) { w.write_bit(1 - b); pending--; }
    };
    for (int i = 0; i < n; i++) {
        int s = in[i];
        uint32_t lo = cum[s], hi = cum[s + 1];
        uint32_t rlo = (uint32_t)(((uint64_t)r * lo) / total);
        uint32_t rhi = (uint32_t)(((uint64_t)r * hi) / total);
        low += rlo;
        r = rhi - rlo;
        if (r == 0) r = 1;
        for (;;) {
            if (low + r <= HALF) {               // 区间全在下半：输出 0
                out_bit(0);
            } else if (low >= HALF) {            // 区间全在上半：输出 1
                out_bit(1);
                low -= HALF;
            } else if (low >= Q1 && low + r <= Q3) {   // 区间在中段 1/4：pending
                pending++;
                low -= Q1;
            } else {
                break;
            }
            low <<= 1;                           // 折叠：区间坐标翻倍
            r <<= 1;
        }
    }
    // 结束：输出 low 区间的高 8 位 + pending 反位（保证流点落在最终区间）
    uint32_t fbit = HALF;
    for (int i = 0; i < 8; i++) {
        out_bit((low >= fbit) ? 1 : 0);
        fbit >>= 1;
    }
    return hdr + w.finish();
}

int arithmetic_decode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    if (n < 512) return -1;
    // 读频率表
    unsigned freq[256];
    memset(freq, 0, sizeof(freq));
    unsigned total = 0;
    for (int i = 0; i < 256; i++) {
        freq[i] = ((unsigned)in[i * 2] << 8) | in[i * 2 + 1];
        total += freq[i];
    }
    if (total == 0) return 0;
    unsigned cum[257];
    cum[0] = 0;
    for (int i = 0; i < 256; i++) cum[i + 1] = cum[i] + freq[i];
    // 待解码长度 = total
    if (total > (unsigned)outcap) return -1;
    BitReader r(in + 512, n - 512);
    uint32_t low = 0;
    uint32_t r_ = TOP;
    // 预读 24 位（与区间精度一致）
    uint32_t code = 0;
    for (int i = 0; i < CODEBITS; i++) code = (code << 1) | (uint32_t)r.read_bit();
    int o = 0;
    while (o < (int)total) {
        // 找符号：value = (code - low) * total / r
        uint32_t val = (uint32_t)(((uint64_t)(code - low) * total) / r_);
        int s = 0;
        while (s < 256 && val >= cum[s + 1]) s++;
        if (s >= 256) s = 255;
        out[o++] = (unsigned char)s;
        uint32_t lo = cum[s], hi = cum[s + 1];
        uint32_t rlo = (uint32_t)(((uint64_t)r_ * lo) / total);
        uint32_t rhi = (uint32_t)(((uint64_t)r_ * hi) / total);
        low += rlo;
        r_ = rhi - rlo;
        if (r_ == 0) r_ = 1;
        for (;;) {
            if (low + r_ <= HALF) {              // 全下半：读 0 位
                // code 保持（其高位已是 0）
            } else if (low >= HALF) {            // 全上半：读 1 位
                low -= HALF;
                if (code >= HALF) code -= HALF;
            } else if (low >= Q1 && low + r_ <= Q3) {   // 中段
                low -= Q1;
                if (code >= Q1) code -= Q1;
            } else {
                break;
            }
            code = (code << 1) | (uint32_t)r.read_bit();
            low <<= 1;
            r_ <<= 1;
        }
    }
    return o;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int arithmetic_self_test() {
    g_fails = 0;
    unsigned char enc[4096], dec[4096];
    // 文本
    {
        const char* txt = "aababbabbaababbaaabbbaaaab";   // 高频 a/b
        int n = (int)strlen(txt);
        int el = arithmetic_encode((const unsigned char*)txt, n, enc, sizeof(enc));
        expect("arith-encode", el > 0);
        int dl = arithmetic_decode(enc, el, dec, sizeof(dec));
        expect("arith-decode", dl == n && memcmp(dec, txt, n) == 0);
    }
    // 单符号
    {
        unsigned char in[16];
        memset(in, 'z', 16);
        int el = arithmetic_encode(in, 16, enc, sizeof(enc));
        expect("arith-single-encode", el > 0);
        int dl = arithmetic_decode(enc, el, dec, sizeof(dec));
        expect("arith-single-decode", dl == 16 && memcmp(dec, in, 16) == 0);
    }
    // 二进制
    {
        unsigned char in[200];
        for (int i = 0; i < 200; i++) in[i] = (unsigned char)((i * 13 + 9) % 251);
        int el = arithmetic_encode(in, 200, enc, sizeof(enc));
        expect("arith-bin-encode", el > 0);
        int dl = arithmetic_decode(enc, el, dec, sizeof(dec));
        expect("arith-bin-decode", dl == 200 && memcmp(dec, in, 200) == 0);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
