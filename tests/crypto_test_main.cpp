// nefuOS 密码学库独立编译测试
// 编译：g++ -std=c++17 -fno-exceptions -fno-rtti -O2 -I core -I core\crypto
//       tests\crypto_test_main.cpp core\crypto\*.cpp core\lib\bigint.cpp core\lib\hash.cpp
//       core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp -o crypto_test.exe
#include "crypto_all.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 宿主桩：klib/memory.cpp 的 operator new 依赖 kalloc/kfree，printf.cpp 依赖 platform_dbg。
// 裸机/宿主正式构建由平台层提供；这里用标准库补齐，便于独立编译验证。
namespace nefu {
void* kalloc(size_t n) { return std::malloc(n ? n : 1); }
void  kfree(void* p) { std::free(p); }
void  platform_dbg(const char*) {}
}

using namespace nefu::crypto;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int total = 0;
    int f;

    f = cipher_self_test();      printf("cipher_self_test   : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = hashx_self_test();       printf("hashx_self_test    : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = mac_self_test();         printf("mac_self_test      : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = kdf_self_test();         printf("kdf_self_test      : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = rng_self_test();         printf("rng_self_test      : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = codec_self_test();       printf("codec_self_test    : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = classic_self_test();     printf("classic_self_test  : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;
    f = pubkey_self_test();      printf("pubkey_self_test   : %s (%d failures)\n", f==0?"PASS":"FAIL", f); total+=f;

    int all = crypto_self_test();
    printf("crypto_self_test (aggregated): %s (%d failures)\n", all==0?"PASS":"FAIL", all);

    printf("\n=== RESULT: %s (total failures=%d) ===\n", total==0?"ALL PASS":"SOME FAILURES", total);
    return total==0 ? 0 : 1;
}
