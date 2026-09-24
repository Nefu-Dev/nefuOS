// nefuOS 深度学习库 —— 独立宿主测试主程序
// 编译：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 ^
//     -I core tests\deeplearn_test_main.cpp core\deeplearn\*.cpp ^
//     core\lib\softmath.cpp core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\deeplearn_test.exe
// 提供 kalloc/kfree/krealloc/platform_dbg 宿主桩，跑全部 self_test。
#include "../core/deeplearn/deeplearn_all.h"
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

using namespace nefu::deeplearn;
namespace fx = nefu::fx;

int main() {
    int ft  = tensor_self_test();
    int fa  = autograd_self_test();
    int fac = activations_self_test();
    int fl  = losses_self_test();
    int fla = layers_self_test();
    int fo  = optimizers_self_test();
    int fr  = rnn_self_test();
    int fat = attention_self_test();
    int fmd = models_self_test();
    int fd  = data_self_test();
    int total = ft+fa+fac+fl+fla+fo+fr+fat+fmd+fd;

    printf("nefuOS deeplearn self test:\n");
    printf("  tensor       failures = %d\n", ft);
    printf("  autograd     failures = %d\n", fa);
    printf("  activations  failures = %d\n", fac);
    printf("  losses       failures = %d\n", fl);
    printf("  layers       failures = %d\n", fla);
    printf("  optimizers   failures = %d\n", fo);
    printf("  rnn          failures = %d\n", fr);
    printf("  attention    failures = %d\n", fat);
    printf("  models       failures = %d\n", fmd);
    printf("  data         failures = %d\n", fd);
    printf("  TOTAL        failures = %d\n", total);
    fflush(stdout);

    // 额外诊断：用 MSE+SGD 拟合 y=2x+1，打印收敛后的 w/b
    printf("diag: linear fit y=2x+1\n"); fflush(stdout);
    {
        tape_reset();
        fix xd[5] = {fx::itofix(0),fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4)};
        fix yd[5];
        for (int i=0;i<5;i++) yd[i] = fx::itofix(2*i+1);
        int s1[1] = {5};
        Tensor x = t_from_flat(1,s1,xd);
        Tensor y = t_from_flat(1,s1,yd);
        Tensor w = t_zeros(1,(int[1]){1}); requires_grad(w);
        Tensor b = t_zeros(1,(int[1]){1}); requires_grad(b);
        SGD opt(fx::fxf(1,10));
        opt.add(&w); opt.add(&b);
        for (int ep=0; ep<200; ep++) {
            tape_reset();
            Tensor wx = t_mul(w, x);
            Tensor pred = t_add(wx, b);
            Tensor diff = t_sub(pred, y);
            Tensor sq = t_pow2(diff);
            Tensor loss = t_mean(sq, 0);
            backward(loss);
            opt.step(); opt.zero_grad();
        }
        printf("  fitted w=%.3f b=%.3f (expect 2.0,1.0)\n",
               w.data[0]/65536.0, b.data[0]/65536.0);
        fflush(stdout);
        tape_reset();
    }

    if (total == 0) printf("ALL DEEPLEARN TESTS PASSED\n");
    else printf("FAILURES DETECTED\n");
    fflush(stdout);
    return total ? 1 : 0;
}
