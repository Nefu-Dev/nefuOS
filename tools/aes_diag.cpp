#include "datlib/../cryptlib/aes.h"
#include <cstdio>
#include <cstring>
// 临时直接 include aes.cpp 的实现细节不可行；改用黑盒：先加密后解密验证单块
int main() {
    unsigned char key[16], pt[16], ct[16], rt[16];
    for (int i = 0; i < 16; i++) { key[i] = (unsigned char)i; pt[i] = (unsigned char)(i * 0x11); }
    nefu::crypt::aes128_encrypt_block(key, pt, ct);
    nefu::crypt::aes128_decrypt_block(key, ct, rt);
    printf("match=%d\n", memcmp(rt, pt, 16) == 0);
    for (int i = 0; i < 16; i++) printf("%02x ", pt[i]); printf("\n");
    for (int i = 0; i < 16; i++) printf("%02x ", rt[i]); printf("\n");
    return 0;
}
