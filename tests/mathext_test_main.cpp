// nefuOS 数学扩展库 —— 独立宿主测试主程序
// 编译：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -O2 ^
//     -I core tests\mathext_test_main.cpp core\mathext\*.cpp ^
//     core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\mathext_test.exe
// 这里只提供 kalloc/kfree/krealloc/platform_dbg 桩，然后跑全部 self_test。
#include "../core/mathext/mathext_all.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace nefu {
// 宿主桩：裸机里 kalloc/kfree 由后端提供，宿主测试用 malloc/free。
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void  platform_dbg(const char* s) { fputs(s, stderr); }
} // namespace nefu

using namespace nefu::mathext;

int main() {
    int fc = complex_self_test();
    int fm = matrix_self_test();
    int fp = poly_self_test();
    int fs = statistics_self_test();
    int fn = numtheory_self_test();
    int fg = geometry_self_test();
    int total = fc + fm + fp + fs + fn + fg;

    printf("mathext self test:\n");
    printf("  complex   failures = %d\n", fc);
    printf("  matrix    failures = %d\n", fm);
    printf("  poly      failures = %d\n", fp);
    printf("  statistics failures = %d\n", fs);
    printf("  numtheory failures = %d\n", fn);
    printf("  geometry  failures = %d\n", fg);
    printf("  TOTAL     failures = %d\n", total);

    // 额外打印几个已知值，人工核对
    double a2[4] = {1, 2, 3, 4};
    Matrix A(2, 2, a2);
    printf("  det([[1,2],[3,4]]) = %f (expect -2)\n", mat_determinant(A));
    Complex c = c_mul(Complex(1, 2), Complex(3, 4));
    printf("  (1+2i)*(3+4i) = %f%+fi (expect -5+10i)\n", c.re, c.im);
    printf("  gcd(48,18) = %lld (expect 6)\n", (long long)gcd(48, 18));
    printf("  is_prime(997)=%d is_prime(561)=%d (expect 1,0)\n",
           (int)is_prime(997), (int)is_prime(561));

    if (total == 0) printf("ALL MATHEXT TESTS PASSED\n");
    else printf("FAILURES DETECTED\n");
    return total ? 1 : 0;
}
