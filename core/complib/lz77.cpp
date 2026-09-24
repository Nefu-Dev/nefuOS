// nefuOS compression library — LZ77 implementation
#include "lz77.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace comp {

namespace {
const int WIN = 4096;      // 窗口大小
const int MAXLEN = 255;    // 单次匹配最大长度
}

int lz77_encode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        // 在窗口 [max(0,i-WIN), i) 中找最长匹配
        int best_dist = 0, best_len = 0;
        int wstart = i - WIN;
        if (wstart < 0) wstart = 0;
        for (int d = wstart; d < i; d++) {
            int len = 0;
            while (i + len < n && len < MAXLEN && in[d + len] == in[i + len]) len++;
            if (len > best_len) { best_len = len; best_dist = i - d; }
            if (best_len >= MAXLEN) break;
        }
        if (best_len >= 3) {
            // 三元组：flag + dist(2) + len + next_char + hasnext
            if (o + 6 > outcap) return -1;
            int hasnext = (i + best_len < n) ? 1 : 0;
            unsigned char nxt = hasnext ? in[i + best_len] : 0;
            out[o++] = 0x00;
            out[o++] = (unsigned char)(best_dist & 0xFF);
            out[o++] = (unsigned char)(best_dist >> 8);
            out[o++] = (unsigned char)best_len;
            out[o++] = nxt;
            out[o++] = (unsigned char)hasnext;
            i += best_len + hasnext;      // next 字符已含在三元组中
        } else {
            if (o + 2 > outcap) return -1;
            out[o++] = 0x01;
            out[o++] = in[i];
            i++;
        }
    }
    return o;
}

int lz77_decode(const unsigned char* in, int n, unsigned char* out, int outcap) {
    int o = 0;
    int i = 0;
    while (i < n) {
        unsigned char flag = in[i++];
        if (flag == 0x00) {
            if (i + 5 > n) return -1;
            int dist = in[i] | (in[i+1] << 8);
            int len = in[i+2];
            unsigned char nxt = in[i+3];
            int hasnext = in[i+4];
            i += 5;
            if (dist <= 0 || dist > o) return -1;
            if (o + len + (hasnext ? 1 : 0) > outcap) return -1;
            for (int k = 0; k < len; k++) out[o] = out[o - dist], o++;
            if (hasnext) out[o++] = nxt;
        } else {
            if (i >= n) return -1;
            if (o + 1 > outcap) return -1;
            out[o++] = in[i++];
        }
    }
    return o;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int lz77_self_test() {
    g_fails = 0;
    unsigned char enc[8192], dec[8192];
    // 重复文本
    {
        const char* txt = "the quick brown fox jumps over the lazy dog. "
                           "the quick brown fox jumps over the lazy dog. "
                           "the quick brown fox jumps over the lazy dog. "
                           "the quick brown fox jumps over the lazy dog. ";
        int n = (int)strlen(txt);
        int el = lz77_encode((const unsigned char*)txt, n, enc, sizeof(enc));
        expect("lz77-encode", el > 0);
        expect("lz77-compresses", el < n);
        int dl = lz77_decode(enc, el, dec, sizeof(dec));
        expect("lz77-decode", dl == n && memcmp(dec, txt, n) == 0);
    }
    // 高度重复："aaaa..." 
    {
        unsigned char in[200];
        memset(in, 'a', 200);
        int el = lz77_encode(in, 200, enc, sizeof(enc));
        expect("lz77-run-encode", el > 0);
        int dl = lz77_decode(enc, el, dec, sizeof(dec));
        expect("lz77-run-decode", dl == 200 && memcmp(dec, in, 200) == 0);
    }
    // 无重复（最坏情况：2 字节/字符）
    {
        unsigned char in[100];
        for (int i = 0; i < 100; i++) in[i] = (unsigned char)(i * 5 + 3);
        int el = lz77_encode(in, 100, enc, sizeof(enc));
        expect("lz77-mixed-encode", el > 0);
        int dl = lz77_decode(enc, el, dec, sizeof(dec));
        expect("lz77-mixed-decode", dl == 100 && memcmp(dec, in, 100) == 0);
    }
    // 距离 > 255（跨窗口长匹配）与窗口边界
    {
        unsigned char in[600];
        // 前 300 随机，后 300 复制
        for (int i = 0; i < 300; i++) in[i] = (unsigned char)(i * 17);
        memcpy(in + 300, in, 300);
        int el = lz77_encode(in, 600, enc, sizeof(enc));
        expect("lz77-window-encode", el > 0);
        int dl = lz77_decode(enc, el, dec, sizeof(dec));
        expect("lz77-window-decode", dl == 600 && memcmp(dec, in, 600) == 0);
    }
    return g_fails;
}

} // namespace comp
} // namespace nefu
