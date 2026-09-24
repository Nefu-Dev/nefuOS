// nefuOS 机器学习库 —— 独立宿主测试主程序
// 编译：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 ^
//     -I core tests\ml_test_main.cpp core\ml\*.cpp ^
//     core\lib\softmath.cpp core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\ml_test.exe
// 这里只提供 kalloc/kfree/krealloc/platform_dbg 桩，然后跑全部 self_test。
// 所有子模块 self_test 必须返回 0。
#include "../core/ml/ml_all.h"
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

using namespace nefu::ml;
namespace fx = nefu::fx;

int main() {
    int fm  = matrix_self_test();
    int fd  = dataset_self_test();
    int fl  = linear_self_test();
    int fk  = knn_self_test();
    int ft  = decisiontree_self_test();
    int fr  = randomforest_self_test();
    int fkm = kmeans_self_test();
    int fkm2 = kmedoids_self_test();
    int fp  = pca_self_test();
    int fnn = neuralnet_self_test();
    int fb  = bayes_self_test();
    int total = fm + fd + fl + fk + ft + fr + fkm + fkm2 + fp + fnn + fb;

    printf("nefuOS ml self test:\n");
    printf("  matrix      failures = %d\n", fm);
    printf("  dataset     failures = %d\n", fd);
    printf("  linear      failures = %d\n", fl);
    printf("  knn         failures = %d\n", fk);
    printf("  decisiontree failures = %d\n", ft);
    printf("  randomforest failures = %d\n", fr);
    printf("  kmeans      failures = %d\n", fkm);
    printf("  pca         failures = %d\n", fp);
    printf("  neuralnet   failures = %d\n", fnn);
    printf("  bayes       failures = %d\n", fb);
    printf("  TOTAL       failures = %d\n", total);
    fflush(stdout);

    // 额外打印几个已知值，人工核对
    // 1) 线性回归正规方程：y=2x+1
    printf("diag1: linear reg\n"); fflush(stdout);
    {
        fix X[5] = {fx::itofix(0), fx::itofix(1), fx::itofix(2), fx::itofix(3), fx::itofix(4)};
        fix y[5];
        for (int i = 0; i < 5; i++) y[i] = fx::itofix(2 * i + 1);
        LinearReg lr;
        lr.fit_normal_eq(X, y, 5, 1);
        printf("  linear reg w=%.3f b=%.3f (expect 2.0,1.0)\n",
               lr.w[0] / 65536.0, lr.b / 65536.0);
    }
    fflush(stdout);
    printf("diag2: kmeans\n"); fflush(stdout);
    // 2) K-Means 分离两个明显簇
    {
        fix X[8][2] = {
            {fx::itofix(0),fx::itofix(0)},{fx::itofix(1),fx::itofix(0)},
            {fx::itofix(0),fx::itofix(1)},{fx::itofix(1),fx::itofix(1)},
            {fx::itofix(10),fx::itofix(10)},{fx::itofix(11),fx::itofix(10)},
            {fx::itofix(10),fx::itofix(11)},{fx::itofix(11),fx::itofix(11)},
        };
        fix flat[16];
        for (int i = 0; i < 8; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }
        KMeans km;
        km.fit(flat, 8, 2, 2, 99);
        printf("  kmeans inertia=%.3f (expect small)\n", km.inertia(flat, 8) / 65536.0);
    }

    if (total == 0) printf("ALL ML TESTS PASSED\n");
    else printf("FAILURES DETECTED\n");
    return total ? 1 : 0;
}
