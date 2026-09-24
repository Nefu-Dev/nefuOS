// nefuOS crypto library — HMAC-SHA256 implementation
#include "hmac.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

void hmac_sha256(const unsigned char* key, int klen,
                 const unsigned char* msg, int mlen, unsigned char out[32]) {
    // 密钥块：<=64 直接复制，>64 先哈希
    unsigned char kblock[64];
    if (klen <= 64) {
        memset(kblock, 0, 64);
        memcpy(kblock, key, klen);
    } else {
        sha256(key, klen, kblock);
        memset(kblock + 32, 0, 32);
    }
    // ipad / opad 异或
    unsigned char ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = kblock[i] ^ 0x36;
        opad[i] = kblock[i] ^ 0x5c;
    }
    // 内层：H((K^ipad) || msg)
    unsigned char inner[64 + 4096];   // 教学上限：消息最多 4096 字节
    memcpy(inner, ipad, 64);
    if (mlen > 4096) mlen = 4096;
    memcpy(inner + 64, msg, mlen);
    unsigned char inner_hash[32];
    sha256(inner, 64 + mlen, inner_hash);
    // 外层：H((K^opad) || inner_hash)
    unsigned char outer[96];
    memcpy(outer, opad, 64);
    memcpy(outer + 64, inner_hash, 32);
    sha256(outer, 96, out);
}

void hmac_sha256_hex(const unsigned char* out32, char* hexbuf) {
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

int hmac_self_test() {
    g_fails = 0;
    unsigned char out[32];
    char hex[65];
    // RFC 4231 测试用例 1：key=0x0b*20，msg="Hi There"
    {
        unsigned char key[20];
        for (int i = 0; i < 20; i++) key[i] = 0x0b;
        hmac_sha256(key, 20, (const unsigned char*)"Hi There", 8, out);
        hmac_sha256_hex(out, hex);
        expect("hmac-rfc1", strcmp(hex, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7") == 0);
    }
    // RFC 4231 测试用例 2：key="Jefe"，msg="what do ya want for nothing?"
    {
        hmac_sha256((const unsigned char*)"Jefe", 4,
                    (const unsigned char*)"what do ya want for nothing?", 28, out);
        hmac_sha256_hex(out, hex);
        expect("hmac-rfc2", strcmp(hex, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843") == 0);
    }
    // RFC 4231 测试用例 3：key=0xaa*20，msg=0xdd*50
    {
        unsigned char key[20], msg[50];
        for (int i = 0; i < 20; i++) key[i] = 0xaa;
        for (int i = 0; i < 50; i++) msg[i] = 0xdd;
        hmac_sha256(key, 20, msg, 50, out);
        hmac_sha256_hex(out, hex);
        expect("hmac-rfc3", strcmp(hex, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe") == 0);
    }
    // 确定性
    hmac_sha256((const unsigned char*)"k", 1, (const unsigned char*)"m", 1, out);
    unsigned char out2[32];
    hmac_sha256((const unsigned char*)"k", 1, (const unsigned char*)"m", 1, out2);
    expect("hmac-deterministic", memcmp(out, out2, 32) == 0);
    // 密钥不同结果不同
    hmac_sha256((const unsigned char*)"k2", 2, (const unsigned char*)"m", 1, out2);
    expect("hmac-key-differs", memcmp(out, out2, 32) != 0);
    return g_fails;
}

} // namespace crypt
} // namespace nefu
