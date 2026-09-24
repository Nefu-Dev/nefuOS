// cryptlib standalone self-test host
#include "cryptlib/crypt_all.h"
#include <cstdio>

int main() {
    int f = nefu::crypt::crypt_all_self_test();
    printf("cryptlib TOTAL=%d\n", f);
    return f;
}
