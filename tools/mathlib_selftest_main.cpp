// nefuOS mathlib 汇总自测宿主
#include <cstdio>
#include "mathlib/math_all.h"

int main() {
    int t = nefu::mathx::math_all_self_test();
    printf("mathlib self tests total failures = %d\n", t);
    return t == 0 ? 0 : 1;
}
