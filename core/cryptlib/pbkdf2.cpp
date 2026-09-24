// nefuOS crypto library — PBKDF2 implementation
#include "pbkdf2.h"
#include "hmac.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

void pbkdf2_sha256(const unsigned char* pwd, int plen,
                   const unsigned char* salt, int slen,
                   int iter, int dklen, unsigned char* out) {
    // 每块 32 字节（SHA256 输出）。U1 = HMAC(P, S || INT_BE(i))
    unsigned char u[32], t[32];
    int blocks = (dklen + 31) / 32;
    for (int blk = 1; blk <= blocks; blk++) {
        // U1
        unsigned char sblock[64 + 4];
        int sl = slen < 64 ? slen : 64;
        memcpy(sblock, salt, sl);
        sblock[sl]     = (unsigned char)(blk >> 24);
        sblock[sl + 1] = (unsigned char)(blk >> 16);
        sblock[sl + 2] = (unsigned char)(blk >> 8);
        sblock[sl + 3] = (unsigned char)blk;
        hmac_sha256(pwd, plen, sblock, sl + 4, u);
        memcpy(t, u, 32);
        // U2..Uc
        for (int it = 1; it < iter; it++) {
            hmac_sha256(pwd, plen, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }
        int off = (blk - 1) * 32;
        int cop = (off + 32 <= dklen) ? 32 : dklen - off;
        memcpy(out + off, t, cop);
    }
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int pbkdf2_self_test() {
    g_fails = 0;
    // RFC 6070 用例 1：P="password", S="salt", c=1, dkLen=20
    {
        unsigned char dk[32];
        pbkdf2_sha256((const unsigned char*)"password", 8,
                      (const unsigned char*)"salt", 4, 1, 32, dk);
        static const unsigned char expect1[32] = {
            0x12,0x0f,0xb6,0xcf,0xfc,0xf8,0xb3,0x2c,0x43,0xe7,
            0x22,0x52,0x56,0xc4,0xf8,0x37,0xa8,0x65,0x48,0xc9,
            0x2c,0xcc,0x35,0x48,0x08,0x05,0x98,0x7c,0xb7,0x0b,0xe1,0x7b
        };
        expect("pbkdf2-rfc1", memcmp(dk, expect1, 32) == 0);
    }
    // RFC 6070 用例 2：P="password", S="salt", c=2, dkLen=20
    {
        unsigned char dk[32];
        pbkdf2_sha256((const unsigned char*)"password", 8,
                      (const unsigned char*)"salt", 4, 2, 32, dk);
        static const unsigned char expect2[32] = {
            0xae,0x4d,0x0c,0x95,0xaf,0x6b,0x46,0xd3,0x2d,0x0a,
            0xdf,0xf9,0x28,0xf0,0x6d,0xd0,0x2a,0x30,0x3f,0x8e,
            0xf3,0xc2,0x51,0xdf,0xd6,0xe2,0xd8,0x5a,0x95,0x47,0x4c,0x43
        };
        expect("pbkdf2-rfc2", memcmp(dk, expect2, 32) == 0);
    }
    // 迭代次数不同 → 结果不同
    {
        unsigned char dk1[32], dk2[32];
        pbkdf2_sha256((const unsigned char*)"pw", 2, (const unsigned char*)"s", 1, 1, 16, dk1);
        pbkdf2_sha256((const unsigned char*)"pw", 2, (const unsigned char*)"s", 1, 3, 16, dk2);
        expect("pbkdf2-iter-differs", memcmp(dk1, dk2, 16) != 0);
    }
    // 盐不同 → 结果不同
    {
        unsigned char dk1[32], dk2[32];
        pbkdf2_sha256((const unsigned char*)"pw", 2, (const unsigned char*)"s1", 2, 2, 16, dk1);
        pbkdf2_sha256((const unsigned char*)"pw", 2, (const unsigned char*)"s2", 2, 2, 16, dk2);
        expect("pbkdf2-salt-differs", memcmp(dk1, dk2, 16) != 0);
    }
    // 长度跨块（40 字节 > 32）
    {
        unsigned char dk[64];
        pbkdf2_sha256((const unsigned char*)"password", 8,
                      (const unsigned char*)"salt", 4, 2, 40, dk);
        expect("pbkdf2-len40", dk[0] == 0xae && dk[39] != 0);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
