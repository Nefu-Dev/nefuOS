// nefuOS 压缩算法库 —— 算术/区间编码实现
//
// QM-coder 简介（教学）：
//   QM-coder 是 JPEG 所用的自适应二进制算术编码器。它维护两个概率区间
//   [0,QE) 和 [QE,1)，根据当前 0/1 的概率估计 QE 不断细分区间，
//   并用 E3 缩放（renormalization）保持精度。本文件实现的是更通用的
//   多符号区间编码器，原理相同：区间不断细分，最后用一个整数代表区间。
#include "arith.h"
#include <stdio.h>
#include "bitio.h"
#include <stdint.h>

namespace nefu {
namespace compress {

// 区间编码常量：32 位寄存器，区间小于此阈值就缩放输出
#define RC_TOP   (1u << 24)
#define RC_BOT   (1u << 16)

namespace {
// 编码器内部状态
struct RCEnc {
    uint8_t* out; int pos; uint32_t low; uint32_t range;
    uint32_t cache; int carries; int cache_cnt;
};
// 输出一个字节（带进位传播）
void rc_byte(RCEnc& e, uint8_t b) {
    e.out[e.pos++] = b;
}
// 重正化：range 太小就把高位字节输出
void rc_renorm(RCEnc& e) {
    while (e.range < RC_TOP) {
        rc_byte(e, (uint8_t)(e.low >> 24));
        e.low <<= 8;
        e.range <<= 8;
    }
}
} // namespace

int arith_encode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (cap < 4 + 256 * 2) return -1;
    put_u32_le(out, (uint32_t)n);
    if (n == 0) return 4;
    // 统计频率
    uint32_t freq[256];
    bfill(freq, 0, sizeof(freq));
    for (int i = 0; i < n; i++) freq[in[i]]++;
    // 写头部：256 个 u16 频率
    for (int c = 0; c < 256; c++) put_u16_le(out + 4 + c * 2, (uint16_t)freq[c]);
    // 区间编码：64 位寄存器，range 初值 2^40，renorm 阈值 2^24，避免移位溢出
    uint32_t cumtab[257];
    cumtab[0] = 0;
    for (int c = 0; c < 256; c++) cumtab[c + 1] = cumtab[c] + freq[c];
    uint64_t low = 0;
    uint64_t range = (uint64_t)1 << 56;
    int pos = 4 + 512;
    for (int i = 0; i < n; i++) {
        uint32_t s = in[i];
        range /= (uint64_t)n;
        low += (uint64_t)cumtab[s] * range;
        range *= freq[s];
        while (range < ((uint64_t)1 << 48)) {
            out[pos++] = (uint8_t)(low >> 56);
            low <<= 8;
            range <<= 8;
        }
    }
    out[pos++] = (uint8_t)(low >> 56);
    out[pos++] = (uint8_t)(low >> 48);
    out[pos++] = (uint8_t)(low >> 40);
    out[pos++] = (uint8_t)(low >> 32);
    out[pos++] = (uint8_t)(low >> 24);
    out[pos++] = (uint8_t)(low >> 16);
    out[pos++] = (uint8_t)(low >> 8);
    out[pos++] = (uint8_t)(low);
    return pos;
}
int arith_decode(const uint8_t* in, int n, uint8_t* out, int cap) {
    if (n < 4) return -1;
    int orig = (int)get_u32_le(in);
    if (orig == 0) return 0;
    if (n < 4 + 512) return -1;
    uint32_t freq[256];
    for (int c = 0; c < 256; c++) freq[c] = get_u16_le(in + 4 + c * 2);
    // 累计频率表
    uint32_t cumtab[257];
    cumtab[0] = 0;
    for (int c = 0; c < 256; c++) cumtab[c + 1] = cumtab[c] + freq[c];
    // 读入编码后的 4 字节到 code
    int p = 4 + 512;
    uint64_t code = 0;
    for (int k = 0; k < 8; k++) code = (code << 8) | in[p++];
    uint64_t low = 0, range = (uint64_t)1 << 56;
    for (int i = 0; i < orig; i++) {
        range /= (uint64_t)orig;
        uint64_t v = (code - low) / range;
        int s = 0;
        while (s < 255 && cumtab[s + 1] <= v) s++;
        out[i] = (uint8_t)s;
        low += (uint64_t)cumtab[s] * range;
        range *= freq[s];
        while (range < ((uint64_t)1 << 48)) {
            code = (code << 8) | in[p++];
            low <<= 8;
            range <<= 8;
        }
    }    return orig;
}

// =====================================================================
// 自测
// =====================================================================
namespace {
int g_fails = 0;
}

int arith_self_test() {
    g_fails = 0;
    const int N = 3000;
    uint8_t* d = new uint8_t[N];
    uint8_t* c = new uint8_t[N * 2 + 600];
    uint8_t* o = new uint8_t[N];

    auto rt = [&](const uint8_t* data, int n) {
        int cl = arith_encode(data, n, c, N * 2 + 600);
        if (cl < 0) { g_fails++; return; }
        int dl = arith_decode(c, cl, o, N);
        bool ok = (dl == n);
        if (ok && n > 0) ok = bcmp(data, o, (size_t)n) == 0;
        if (!ok) { g_fails++; fprintf(stderr,"rt fail n=%d cl=%d dl=%d\n",n,cl,dl); }
    };

    // 1) 重复文本（高压缩率）
    {
        const char* t = "abracadabra abracadabra ";
        int tl = blen(t); int k = 0;
        for (int i = 0; i < N; i++) d[i] = (uint8_t)t[k++ % tl];
        rt(d, N);
    }
    // 2) 随机
    uint32_t seed = 42;
    for (int i = 0; i < N; i++) { seed = seed*1664525u+1013904223u; d[i] = (uint8_t)(seed & 0xFF); }
    rt(d, N);
    // 3) 全零
    bfill(d, 0, N);
    rt(d, N);
    // 4) 边界
    rt(d, 0);
    d[0] = 77; rt(d, 1);
    // 5) 压缩比验证：重复文本应明显变小
    {
        const char* t = "hello world hello world ";
        int tl = blen(t); int k = 0;
        for (int i = 0; i < N; i++) d[i] = (uint8_t)t[k++ % tl];
        int cl = arith_encode(d, N, c, N * 2 + 600);
        if (cl >= N) g_fails++;   // 重复文本必须压缩
    }
    delete[] d; delete[] c; delete[] o;
    return g_fails;
}

} // namespace compress
} // namespace nefu
