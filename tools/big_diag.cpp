#include <cstdio>
#include "mathlib/bigint.h"

int main() {
    nefu::mathx::BigInt e("12345678901234567890"), f("1000000007");
    nefu::mathx::BigInt q2 = e / f;
    nefu::mathx::BigInt r2 = e % f;
    printf("q=%s\n", q2.to_string().c_str());
    printf("r=%s\n", r2.to_string().c_str());
    nefu::mathx::BigInt chk = q2; chk.mul(f); chk.add(r2);
    printf("q*f+r=%s\n", chk.to_string().c_str());
    // 手动验证期望商
    nefu::mathx::BigInt exp("12345678899");
    printf("exp*f+r = ");
    nefu::mathx::BigInt t = exp; t.mul(f); t.add(r2);
    printf("%s\n", t.to_string().c_str());
    return 0;
}
