// nefuOS crypto library — Base64/Base32 implementation
#include "b64.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char* B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

int base64_encode(const unsigned char* data, int n, char* out) {
    int o = 0;
    int i = 0;
    for (; i + 2 < n; i += 3) {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i+1] << 8) | data[i+2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = B64[v & 63];
    }
    int rem = n - i;
    if (rem == 1) {
        unsigned v = (unsigned)data[i] << 16;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i+1] << 8);
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = '=';
    }
    out[o] = 0;
    return o;
}

// 反查表：-1 = 非法
static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int base64_decode(const char* in, int n, unsigned char* out) {
    int o = 0;
    int v = 0;
    int bits = 0;
    for (int i = 0; i < n; i++) {
        char c = in[i];
        if (c == '=' || c == '\n' || c == '\r') continue;
        int d = b64_val(c);
        if (d < 0) return -1;
        v = (v << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((v >> bits) & 0xFF);
        }
    }
    return o;
}

void base64_str(const char* s, char* out) {
    base64_encode((const unsigned char*)s, (int)strlen(s), out);
}

int base32_encode(const unsigned char* data, int n, char* out) {
    // 5 位一组：每 8 字节输入 → 13 个字符（40 位）
    int o = 0;
    int bitbuf = 0;
    int bits = 0;
    for (int i = 0; i < n; i++) {
        bitbuf = (bitbuf << 8) | data[i];
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out[o++] = B32[(bitbuf >> bits) & 31];
        }
    }
    if (bits > 0) out[o++] = B32[(bitbuf << (5 - bits)) & 31];
    out[o] = 0;
    return o;
}

static int b32_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

int base32_decode(const char* in, int n, unsigned char* out) {
    int o = 0;
    int bitbuf = 0;
    int bits = 0;
    for (int i = 0; i < n; i++) {
        char c = in[i];
        if (c == '=' || c == '\n' || c == '\r') continue;
        int d = b32_val(c);
        if (d < 0) return -1;
        bitbuf = (bitbuf << 5) | d;
        bits += 5;
        while (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((bitbuf >> bits) & 0xFF);
        }
    }
    return o;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int b64_self_test() {
    g_fails = 0;
    char enc[128];
    unsigned char dec[96];
    // 标准向量："hello" -> aGVsbG8=
    base64_str("hello", enc);
    expect("b64-hello", strcmp(enc, "aGVsbG8=") == 0);
    // "Man" -> TWFu
    base64_str("Man", enc);
    expect("b64-man", strcmp(enc, "TWFu") == 0);
    // "M" -> TQ==
    base64_str("M", enc);
    expect("b64-m", strcmp(enc, "TQ==") == 0);
    // 空
    base64_encode((const unsigned char*)"", 0, enc);
    expect("b64-empty", strcmp(enc, "") == 0);
    // 解码往返
    const char* samples[] = { "aGVsbG8=", "TWFu", "TQ==", "aGVsbG8gd29ybGQ=", "" };
    for (int i = 0; i < 5; i++) {
        int len = base64_decode(samples[i], (int)strlen(samples[i]), dec);
        base64_encode(dec, len, enc);
        expect("b64-roundtrip", strcmp(enc, samples[i]) == 0);
    }
    // 二进制随机数据往返
    {
        unsigned char raw[64];
        for (int i = 0; i < 64; i++) raw[i] = (unsigned char)(i * 37 + 11);
        int elen = base64_encode(raw, 64, enc);
        int dlen = base64_decode(enc, elen, dec);
        bool ok = (dlen == 64);
        for (int i = 0; i < 64 && ok; i++) if (dec[i] != raw[i]) ok = false;
        expect("b64-binary", ok);
    }
    // Base32："hello" -> NBSWY3DP
    base32_encode((const unsigned char*)"hello", 5, enc);
    expect("b32-hello", strcmp(enc, "NBSWY3DP") == 0);
    // 往返
    {
        int len = base32_decode("NBSWY3DP", 8, dec);
        bool ok = (len == 5 && memcmp(dec, "hello", 5) == 0);
        expect("b32-roundtrip", ok);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
