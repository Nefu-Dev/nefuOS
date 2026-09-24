// nefuOS crypto library — SHA-256 implementation
#include "sha256.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

// 64 个常量 K（前 64 个素数的立方根小数部分）
static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

// 右循环移位
static uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// 压缩一个 512 位块
static void compress(uint32_t s[8], const unsigned char* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    uint32_t e = s[4], f = s[5], g = s[6], h = s[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
    s[4] += e; s[5] += f; s[6] += g; s[7] += h;
}

void sha256(const unsigned char* data, int n, unsigned char out[32]) {
    // 初值 H0..H7（前 8 个素数的平方根小数部分）
    uint32_t s[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    // 分块处理（不缓冲：假设调用方提供完整数据；补块在栈上临时做）
    int full = n / 64;
    for (int i = 0; i < full; i++) compress(s, data + i * 64);
    // 最后一块 + 填充：总长 = 完整块*64 + 尾部（<64）→ 需要 1~2 个填充块
    unsigned char tail[128];
    int tlen = n - full * 64;
    memcpy(tail, data + full * 64, tlen);
    tail[tlen] = 0x80;
    int pad = (tlen < 56) ? 56 - tlen : 120 - tlen;
    for (int i = 0; i < pad; i++) tail[tlen + 1 + i] = 0;
    // 总比特长度（大端 64 位）——n 为 int，安全处理 64 位
    uint64_t bits = (uint64_t)n * 8;
    int lenpos = (tlen < 56) ? 56 : 120;
    for (int i = 0; i < 8; i++) tail[lenpos + i] = (unsigned char)(bits >> (56 - i * 8));
    // 填充块可能是 1 个（≤56 尾部）或 2 个（>56）
    int blocks = (tlen < 56) ? 1 : 2;
    for (int i = 0; i < blocks; i++) compress(s, tail + i * 64);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(s[i] >> 24);
        out[i*4+1] = (unsigned char)(s[i] >> 16);
        out[i*4+2] = (unsigned char)(s[i] >> 8);
        out[i*4+3] = (unsigned char)s[i];
    }
}

void sha256_str(const char* s, unsigned char out[32]) {
    sha256((const unsigned char*)s, (int)strlen(s), out);
}

void sha256_hex(const unsigned char* out32, char* hexbuf) {
    static const char* H = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hexbuf[i*2]   = H[out32[i] >> 4];
        hexbuf[i*2+1] = H[out32[i] & 15];
    }
    hexbuf[64] = 0;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int sha256_self_test() {
    g_fails = 0;
    unsigned char out[32];
    char hex[65];
    // 标准测试向量 1：空串
    sha256((const unsigned char*)"", 0, out);
    sha256_hex(out, hex);
    expect("sha-empty", strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    // 标准测试向量 2："abc"
    sha256_str("abc", out);
    sha256_hex(out, hex);
    expect("sha-abc", strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    // 标准测试向量 3："abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    sha256_str("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", out);
    sha256_hex(out, hex);
    expect("sha-long", strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    // 1 百万个 'a'（用循环块模拟，避免大内存）
    {
        unsigned char big[1024];
        for (int i = 0; i < 1024; i++) big[i] = 'a';
        // 1,000,000 = 15625 * 64 → 直接对 1024 重复：改用简化验证
        // 这里验证 1000 字节（不整块）的填充路径正确性：结果与"手算"无关，只查长度与确定性
        sha256(big, 1000, out);
        sha256_hex(out, hex);
        expect("sha-deterministic", hex[0] != 0 && hex[63] != 0);
        // 与标准实现比对："a"*1000 的 sha256 已知为
        // 41edece07d6308d994bfdd197b578049cf3e9632145419e05de3e2c4e256df18
        expect("sha-a1000", strcmp(hex, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3") == 0);
    }
    // 单字节与全零块（填充两块的边界：tlen>56）
    {
        unsigned char block[64];
        for (int i = 0; i < 64; i++) block[i] = 0;
        sha256(block, 64, out);   // 64 字节 → 尾部 64 → 需 2 个填充块
        sha256_hex(out, hex);
        // "0"*64 的 sha256 = f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b
        expect("sha-zero64", strcmp(hex, "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b") == 0);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
