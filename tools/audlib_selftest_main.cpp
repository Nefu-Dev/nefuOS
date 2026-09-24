// nefuOS audlib 汇总自测宿主
#include <cstdio>
#include "audlib/aud_all.h"

int main() {
    int t = nefu::audx::aud_all_self_test();
    printf("audlib self tests total failures = %d\n", t);
    return t == 0 ? 0 : 1;
}
