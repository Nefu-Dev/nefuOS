// nefuOS 仿真引擎库 —— 独立主机测试入口
// 编译命令：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -O2 ^
//     -I core tests\simulate_test_main.cpp core\simulate\*.cpp core\gfxlib\*.cpp ^
//     core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\simulate_test.exe
//
// 本文件提供桩 (kalloc/kfree) 以链接 klib 的 new/delete 实现。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/simulate/simulate_all.h"

// ---- 桩：kalloc/kfree 直接用 malloc/free ----
namespace nefu {
void* kalloc(size_t sz) { return malloc(sz ? sz : 1); }
void  kfree(void* p) { free(p); }
void* krealloc(void* p, size_t sz) { return realloc(p, sz); }
} // namespace nefu

// ---- 桩：gfxlib 里可能引用的平台函数（如果链接报错再补）----
#include "../core/platform.h"
namespace nefu {
uint32_t platform_tick_ms() { return 0; }
void platform_dbg(const char* s) { fputs(s, stdout); }
} // namespace nefu

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int f = 0;
    printf("=== nefuOS simulate library self test ===\n");
    int ca = nefu::simulate::ca_self_test();
    printf("ca        : %d failures\n", ca); f += ca;
    int p  = nefu::simulate::particle_self_test();
    printf("particle  : %d failures\n", p); f += p;
    int ph = nefu::simulate::physics_self_test();
    printf("physics   : %d failures\n", ph); f += ph;
    int fl = nefu::simulate::fluid_self_test();
    printf("fluid     : %d failures\n", fl); f += fl;
    int ls = nefu::simulate::lsystem_self_test();
    printf("lsystem   : %d failures\n", ls); f += ls;
    int fp = nefu::simulate::flocking_self_test();
    printf("flocking  : %d failures\n", fp); f += fp;
    int total = nefu::simulate::simulate_self_test();
    printf("--- total  : %d failures (expected 0) ---\n", total);
    if (total == 0) {
        printf("ALL SIMULATE TESTS PASSED\n");
        return 0;
    }
    printf("FAILURES DETECTED\n");
    return 1;
}
