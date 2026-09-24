// nefuOS hash & encoding library — implementation
#include "hash.h"

namespace nefu {
namespace hash {

// ===================== MD5 =====================

static inline uint32_t rotl32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

static const uint32_t MD5_K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static const int MD5_S[64] = {
    7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
    5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
    4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
    6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static inline uint32_t load32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void store32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void MD5::init() {
    a = 0x67452301; b = 0xefcdab89; c = 0x98badcfe; d = 0x10325476;
    len = 0; buf_len = 0;
}

static void md5_block(MD5& ctx, const uint8_t* p) {
    uint32_t m[16];
    for (int i = 0; i < 16; i++) m[i] = load32le(p + i * 4);
    uint32_t A = ctx.a, B = ctx.b, C = ctx.c, D = ctx.d;
    for (int i = 0; i < 64; i++) {
        uint32_t F, g;
        if (i < 16) { F = (B & C) | (~B & D); g = i; }
        else if (i < 32) { F = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
        else if (i < 48) { F = B ^ C ^ D; g = (3 * i + 5) % 16; }
        else { F = C ^ (B | ~D); g = (7 * i) % 16; }
        uint32_t tmp = D;
        D = C; C = B;
        B = B + rotl32(A + F + MD5_K[i] + m[g], MD5_S[i]);
        A = tmp;
    }
    ctx.a += A; ctx.b += B; ctx.c += C; ctx.d += D;
}

void MD5::update(const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    len += n;
    if (buf_len) {
        while (n && buf_len < 64) { buf[buf_len++] = *p++; n--; }
        if (buf_len == 64) { md5_block(*this, buf); buf_len = 0; }
    }
    while (n >= 64) {
        md5_block(*this, p);
        p += 64; n -= 64;
    }
    while (n) { buf[buf_len++] = *p++; n--; }
}

void MD5::final(uint8_t out[16]) {
    uint64_t bits = len * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (buf_len != 56) update(&zero, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (8 * i));
    update(lenb, 8);
    store32le(out, a); store32le(out + 4, b); store32le(out + 8, c); store32le(out + 12, d);
}

void MD5::hex_final(char out[33]) {
    uint8_t d[16];
    final(d);
    const char* H = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i * 2] = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 15];
    }
    out[32] = 0;
}

// ===================== SHA1 =====================

static inline uint32_t rol32(uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

void SHA1::init() {
    h[0] = 0x67452301; h[1] = 0xEFCDAB89; h[2] = 0x98BADCFE; h[3] = 0x10325476; h[4] = 0xC3D2E1F0;
    len = 0; buf_len = 0;
}

static void sha1_block(SHA1& ctx, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = load32le(p + i * 4);
    for (int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = ctx.h[0], b = ctx.h[1], c = ctx.h[2], d = ctx.h[3], e = ctx.h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t tmp = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = tmp;
    }
    ctx.h[0] += a; ctx.h[1] += b; ctx.h[2] += c; ctx.h[3] += d; ctx.h[4] += e;
}

void SHA1::update(const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    len += n;
    if (buf_len) {
        while (n && buf_len < 64) { buf[buf_len++] = *p++; n--; }
        if (buf_len == 64) { sha1_block(*this, buf); buf_len = 0; }
    }
    while (n >= 64) { sha1_block(*this, p); p += 64; n -= 64; }
    while (n) { buf[buf_len++] = *p++; n--; }
}

void SHA1::final(uint8_t out[20]) {
    uint64_t bits = len * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (buf_len != 56) update(&zero, 1);
    uint8_t lenb[8];
    for (int i = 0; i < 8; i++) lenb[i] = (uint8_t)(bits >> (8 * (7 - i)));
    update(lenb, 8);
    for (int i = 0; i < 5; i++) store32le(out + i * 4, h[i]);
}

void SHA1::hex_final(char out[41]) {
    uint8_t d[20];
    final(d);
    const char* H = "0123456789abcdef";
    for (int i = 0; i < 20; i++) {
        out[i * 2] = H[d[i] >> 4];
        out[i * 2 + 1] = H[d[i] & 15];
    }
    out[40] = 0;
}

// ===================== CRC32 =====================

static uint32_t crc_table[256];
static bool crc_ready = false;

static void crc_init_table() {
    if (crc_ready) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_ready = true;
}

uint32_t crc32(const void* data, size_t n, uint32_t seed) {
    crc_init_table();
    uint32_t c = ~seed;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

// ===================== Adler32 =====================

uint32_t adler32(const void* data, size_t n, uint32_t seed) {
    uint32_t a = seed & 0xFFFF;
    uint32_t b = (seed >> 16) & 0xFFFF;
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

// ===================== Base64 =====================

int base64_encode(const uint8_t* in, size_t in_len, char* out, size_t out_cap) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t need = ((in_len + 2) / 3) * 4 + 1;
    if (out_cap < need) return -1;
    size_t o = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len) v |= (uint32_t)in[i + 2];
        out[o++] = T[(v >> 18) & 63];
        out[o++] = T[(v >> 12) & 63];
        out[o++] = (i + 1 < in_len) ? T[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < in_len) ? T[v & 63] : '=';
    }
    out[o] = 0;
    return (int)o;
}

int base64_decode(const char* in, uint8_t* out, size_t out_cap) {
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *in; in++) {
        int v = T[(uint8_t)*in];
        if (v < 0) continue; // skip whitespace/newlines/padding
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o < out_cap) out[o++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return (int)o;
}

// ===================== Hex =====================

void hex_encode(const uint8_t* in, size_t n, char* out, bool upper) {
    const char* H = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = H[in[i] >> 4];
        out[i * 2 + 1] = H[in[i] & 15];
    }
    out[n * 2] = 0;
}

int hex_decode(const char* in, uint8_t* out, size_t out_cap) {
    size_t o = 0;
    int hi = -1;
    for (; *in; in++) {
        char c = *in;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else continue;
        if (hi < 0) hi = d;
        else {
            if (o < out_cap) out[o++] = (uint8_t)((hi << 4) | d);
            hi = -1;
        }
    }
    return (int)o;
}

} // namespace hash
} // namespace nefu
