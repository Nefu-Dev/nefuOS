// nefuOS compression library — RLE implementation
#include "rle.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

int rle_encode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        // 找游程长度
        int run = 1;
        while (i + run < n && in[i + run] == in[i] && run < 255) run++;
        if (run >= 3) {
            if (o + 3 > outcap) return -1;
            out[o++] = 0x00;              // 标记：重复段
            out[o++] = (unsigned char)run;
            out[o++] = in[i];
            i += run;
        } else {
            // 非重复段：找最长"无 3 连相同"的片段
            int start = i;
            int end = i + 1;
            while (end < n) {
                // 检查从 end 起是否有 3 连
                if (end + 1 < n && in[end] == in[end + 1]) {
                    if (end > start && in[end - 1] == in[end]) break;  // 已经 3 连
                }
                if (end >= start + 2 && in[end] == in[end - 1] && in[end] == in[end - 2]) break;
                end++;
            }
            // 编码普通段：0x00 单独转义为 count=1 的重复段，其余原样输出
            for (int k = start; k < end; k++) {
                if (in[k] == 0x00) {
                    if (o + 3 > outcap) return -1;
                    out[o++] = 0x00;
                    out[o++] = 0x01;
                    out[o++] = 0x00;
                } else {
                    if (o + 1 > outcap) return -1;
                    out[o++] = in[k];
                }
            }
            i = end;
        }
    }
    return o;
}

int rle_decode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        unsigned char tag = in[i++];
        if (tag == 0x00) {               // 重复段
            if (i + 2 > n) return -1;
            int run = in[i++];
            unsigned char v = in[i++];
            if (o + run > outcap) return -1;
            memset(out + o, v, run);
            o += run;
        } else {                          // 普通段：裸字节原样输出
            if (o + 1 > outcap) return -1;
            out[o++] = tag;
        }
    }
    return o;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int rle_self_test() {
    g_fails = 0;
    unsigned char enc[512], dec[512];
    // 强重复数据
    {
        unsigned char in[64];
        memset(in, 0xAA, 64);
        int el = rle_encode(in, 64, enc, sizeof(enc));
        expect("rle-encode-run", el > 0 && el < 64);
        int dl = rle_decode(enc, el, dec, sizeof(dec));
        expect("rle-decode-run", dl == 64 && memcmp(dec, in, 64) == 0);
    }
    // 无重复数据（最坏情况略膨胀）
    {
        unsigned char in[100];
        for (int i = 0; i < 100; i++) in[i] = (unsigned char)i;
        int el = rle_encode(in, 100, enc, sizeof(enc));
        expect("rle-encode-mixed", el > 0);
        int dl = rle_decode(enc, el, dec, sizeof(dec));
        expect("rle-decode-mixed", dl == 100 && memcmp(dec, in, 100) == 0);
    }
    // 混合：重复 + 随机
    {
        unsigned char in[200];
        int k = 0;
        for (int i = 0; i < 20; i++) { for (int j = 0; j < 5; j++) in[k++] = (unsigned char)('A' + i); }
        for (int i = 0; i < 100; i++) in[k++] = (unsigned char)(i * 7);
        int el = rle_encode(in, k, enc, sizeof(enc));
        expect("rle-encode-mix2", el > 0);
        int dl = rle_decode(enc, el, dec, sizeof(dec));
        expect("rle-decode-mix2", dl == k && memcmp(dec, in, k) == 0);
    }
    // 空输入
    {
        int el = rle_encode((const unsigned char*)"", 0, enc, sizeof(enc));
        expect("rle-empty", el == 0);
    }
    // 含 0x00 的普通数据（转义往返）
    {
        unsigned char in[40];
        int k = 0;
        for (int i = 0; i < 40; i++) in[k++] = (unsigned char)(i % 2 == 0 ? 0x00 : i);
        int el = rle_encode(in, k, enc, sizeof(enc));
        expect("rle-encode-zero", el > 0);
        int dl = rle_decode(enc, el, dec, sizeof(dec));
        expect("rle-decode-zero", dl == k && memcmp(dec, in, k) == 0);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
