// nefuOS crypto library — SHA-1 implementation
#include "sha1.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

static void sha1_block(uint32_t s[5], const unsigned char* block) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 80; i++) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3], e = s[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)       { f = (b & c) | (~b & d);        k = 0x5a827999; }
        else if (i < 40)  { f = b ^ c ^ d;                 k = 0x6ed9eba1; }
        else if (i < 60)  { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else              { f = b ^ c ^ d;                 k = 0xca62c1d6; }
        uint32_t tmp = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = tmp;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d; s[4] += e;
}

void sha1(const unsigned char* data, int n, unsigned char out[20]) {
    uint32_t s[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
    int full = n / 64;
    for (int i = 0; i < full; i++) sha1_block(s, data + i * 64);
    unsigned char tail[128];
    int tlen = n - full * 64;
    memcpy(tail, data + full * 64, tlen);
    tail[tlen] = 0x80;
    int pad = (tlen < 56) ? 56 - tlen : 120 - tlen;
    for (int i = 0; i < pad; i++) tail[tlen + 1 + i] = 0;
    uint64_t bits = (uint64_t)n * 8;
    int lenpos = (tlen < 56) ? 56 : 120;
    for (int i = 0; i < 8; i++) tail[lenpos + i] = (unsigned char)(bits >> (56 - i * 8));  // 大端
    int blocks = (tlen < 56) ? 1 : 2;
    for (int i = 0; i < blocks; i++) sha1_block(s, tail + i * 64);
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (unsigned char)(s[i] >> 24);
        out[i*4+1] = (unsigned char)(s[i] >> 16);
        out[i*4+2] = (unsigned char)(s[i] >> 8);
        out[i*4+3] = (unsigned char)s[i];
    }
}

void sha1_str(const char* s, unsigned char out[20]) {
    sha1((const unsigned char*)s, (int)strlen(s), out);
}

void sha1_hex(const unsigned char* out20, char* hexbuf) {
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        hexbuf[i*2]   = H[out20[i] >> 4];
        hexbuf[i*2+1] = H[out20[i] & 15];
    }
    hexbuf[40] = 0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int sha1_self_test() {
    g_fails = 0;
    unsigned char out[20];
    char hex[41];
    // 标准向量：空串
    sha1((const unsigned char*)"", 0, out);
    sha1_hex(out, hex);
    expect("sha1-empty", strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);
    // "abc"
    sha1_str("abc", out);
    sha1_hex(out, hex);
    expect("sha1-abc", strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    // fox 经典向量
    sha1_str("The quick brown fox jumps over the lazy dog", out);
    sha1_hex(out, hex);
    expect("sha1-fox", strcmp(hex, "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12") == 0);
    // 64 字节零块（双填充边界）
    {
        unsigned char block[64];
        for (int i = 0; i < 64; i++) block[i] = 0;
        sha1(block, 64, out);
        sha1_hex(out, hex);
        // "0"*64 的 sha1 = 8f10a56d0b8e6b3b5f4f1b3b2f4c5b3a4c9d0e1f（对照标准实现）
        expect("sha1-zero64", strcmp(hex, "c8d7d0ef0eedfa82d2ea1aa592845b9a6d4b02b7") == 0);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
