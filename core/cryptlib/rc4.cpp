// nefuOS crypto library — RC4 implementation
#include "rc4.h"
#include <stdio.h>
#include <string.h>

namespace nefu {
namespace crypt {

void rc4(const unsigned char* key, int klen, unsigned char* data, int n) {
    // KSA：用密钥打乱状态数组
    unsigned char s[256];
    for (int i = 0; i < 256; i++) s[i] = (unsigned char)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + s[i] + key[i % klen]) & 0xFF;
        unsigned char t = s[i]; s[i] = s[j]; s[j] = t;
    }
    // PRGA：生成密钥流并异或
    int i = 0;
    j = 0;
    for (int k = 0; k < n; k++) {
        i = (i + 1) & 0xFF;
        j = (j + s[i]) & 0xFF;
        unsigned char t = s[i]; s[i] = s[j]; s[j] = t;
        data[k] ^= s[(s[i] + s[j]) & 0xFF];
    }
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
} // namespace

int rc4_self_test() {
    g_fails = 0;
    // 对称性：两次加密还原
    {
        unsigned char key[] = "nefuos-secret-key";
        unsigned char msg[] = "Hello, RC4 stream cipher!";
        int n = (int)strlen((const char*)msg);
        unsigned char a[64], b[64];
        memcpy(a, msg, n);
        rc4(key, 16, a, n);            // 加密
        memcpy(b, a, n);
        rc4(key, 16, b, n);            // 解密
        expect("rc4-roundtrip", memcmp(b, msg, n) == 0);
    }
    // 密钥流确定性：同密钥同数据结果一致
    {
        unsigned char key[] = "key";
        unsigned char m1[] = "abc", m2[] = "abc";
        rc4(key, 3, m1, 3);
        rc4(key, 3, m2, 3);
        expect("rc4-deterministic", m1[0] == m2[0] && m1[1] == m2[1] && m1[2] == m2[2]);
    }
    // 密钥不同结果不同
    {
        unsigned char k1[] = "key1", k2[] = "key2";
        unsigned char m1[] = "abc", m2[] = "abc";
        rc4(k1, 4, m1, 3);
        rc4(k2, 4, m2, 3);
        expect("rc4-key-differs", m1[0] != m2[0] || m1[1] != m2[1] || m1[2] != m2[2]);
    }
    // 标准向量：Key="Key", Plaintext="Plaintext" → 密文 BBF316E8D940AF0AD3
    {
        unsigned char data[] = "Plaintext";
        rc4((const unsigned char*)"Key", 3, data, 9);
        static const unsigned char vec_ct[] = { 0xbb,0xf3,0x16,0xe8,0xd9,0x40,0xaf,0x0a,0xd3 };
        expect("rc4-vector", memcmp(data, vec_ct, 9) == 0);
    }
    return g_fails;
}

} // namespace crypt
} // namespace nefu
