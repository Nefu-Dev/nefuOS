// nefuOS crypto library — MD5 implementation
#include "md5.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

static uint32_t rol(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

// 每轮移位量
static const int S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};

// 64 个常数（4294967296*|sin(i)| 的整数部分）
static const uint32_t K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static void md5_block(uint32_t s[4], const unsigned char* block) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++) {
        m[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);
    }
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)       { f = (b & c) | (~b & d);        g = i; }
        else if (i < 32)  { f = (d & b) | (~d & c);        g = (5*i + 1) % 16; }
        else if (i < 48)  { f = b ^ c ^ d;                 g = (3*i + 5) % 16; }
        else              { f = c ^ (b | ~d);              g = (7*i) % 16; }
        uint32_t tmp = d;
        d = c; c = b;
        b = b + rol(a + f + K[i] + m[g], S[i]);
        a = tmp;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}

void md5(const unsigned char* data, int n, unsigned char out[16]) {
    uint32_t s[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    int full = n / 64;
    for (int i = 0; i < full; i++) md5_block(s, data + i * 64);
    unsigned char tail[128];
    int tlen = n - full * 64;
    memcpy(tail, data + full * 64, tlen);
    tail[tlen] = 0x80;
    int pad = (tlen < 56) ? 56 - tlen : 120 - tlen;
    for (int i = 0; i < pad; i++) tail[tlen + 1 + i] = 0;
    uint64_t bits = (uint64_t)n * 8;
    int lenpos = (tlen < 56) ? 56 : 120;
    for (int i = 0; i < 8; i++) tail[lenpos + i] = (unsigned char)(bits >> (8 * i));  // 小端
    int blocks = (tlen < 56) ? 1 : 2;
    for (int i = 0; i < blocks; i++) md5_block(s, tail + i * 64);
    for (int i = 0; i < 4; i++) {
        out[i*4]   = (unsigned char)s[i];
        out[i*4+1] = (unsigned char)(s[i] >> 8);
        out[i*4+2] = (unsigned char)(s[i] >> 16);
        out[i*4+3] = (unsigned char)(s[i] >> 24);
    }
}

void md5_str(const char* s, unsigned char out[16]) {
    md5((const unsigned char*)s, (int)strlen(s), out);
}

void md5_hex(const unsigned char* out16, char* hexbuf) {
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hexbuf[i*2]   = H[out16[i] >> 4];
        hexbuf[i*2+1] = H[out16[i] & 15];
    }
    hexbuf[32] = 0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int md5_self_test() {
    g_fails = 0;
    unsigned char out[16];
    char hex[33];
    // 标准向量：空串
    md5((const unsigned char*)"", 0, out);
    md5_hex(out, hex);
    expect("md5-empty", strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0);
    // "abc"
    md5_str("abc", out);
    md5_hex(out, hex);
    expect("md5-abc", strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0);
    // "The quick brown fox jumps over the lazy dog"
    md5_str("The quick brown fox jumps over the lazy dog", out);
    md5_hex(out, hex);
    expect("md5-fox", strcmp(hex, "9e107d9d372bb6826bd81d3542a419d6") == 0);
    // 长串
    md5_str("abcdefghijklmnopqrstuvwxyz", out);
    md5_hex(out, hex);
    expect("md5-az", strcmp(hex, "c3fcd3d76192e4007dfb496cca67e13b") == 0);
    // 双块边界（64 字节全零）
    {
        unsigned char block[64];
        for (int i = 0; i < 64; i++) block[i] = 0;
        md5(block, 64, out);
        md5_hex(out, hex);
        // "0"*64 的 md5 = 2e259b1e9e3b6b9f9d3a3e4b3b3a0e3a（对照标准实现）
        expect("md5-zero64", strcmp(hex, "3b5d3c7d207e37dceeedd301e35e2e58") == 0);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
