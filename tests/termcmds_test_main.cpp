// =============================================================================
//  termcmds_test_main.cpp —— 终端命令扩展库独立单元测试入口
// -----------------------------------------------------------------------------
//  编译(在 nefuOS 根目录):
//    D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti ^
//        -fno-builtin -O2 -I core ^
//        tests/termcmds_test_main.cpp ^
//        core/klib/memory.cpp core/klib/string.cpp ^
//        core/termcmds/termcmds_all.cpp core/termcmds/filecmd.cpp ^
//        core/termcmds/textcmd.cpp core/termcmds/syscmd.cpp ^
//        core/termcmds/netcmd.cpp core/termcmds/devcmd.cpp ^
//        core/termcmds/funcmd.cpp ^
//        -o build/termcmds_test.exe
//  运行 build/termcmds_test.exe ; 退出码 0 = 全部通过。
//
//  说明: 本测试是宿主(Windows)可执行文件, 因此用 malloc 实现 nefu::kalloc,
//  不依赖任何后端。被测代码本身不引用 platform 的其它函数。
// =============================================================================
#include <cstdlib>
#include <cstdio>

#include "termcmds/termcmds_all.h"

// ---- 宿主内存垫片: 接 klib operator new/delete ----
namespace nefu {
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void  kfree(void* p)    { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
} // namespace nefu

int main() {
    int total = 0;
    int r;

    std::printf("==== nefuOS termcmds host self-test ====\n");

    r = nefu::termcmds::filecmd_self_test();
    std::printf("filecmd: %s (%d failures)\n", r ? "FAIL" : "OK", r);
    total += r;

    r = nefu::termcmds::textcmd_self_test();
    std::printf("textcmd: %s (%d failures)\n", r ? "FAIL" : "OK", r);
    total += r;

    r = nefu::termcmds::syscmd_self_test();
    std::printf("syscmd:  %s (%d failures)\n", r ? "FAIL" : "OK", r);
    total += r;

    r = nefu::termcmds::netcmd_self_test();
    std::printf("netcmd:  %s (%d failures)\n", r ? "FAIL" : "OK", r);
    total += r;

    r = nefu::termcmds::devcmd_self_test();
    std::printf("devcmd:  %s (%d failures)\n", r ? "FAIL" : "OK", r);
    total += r;

    r = nefu::termcmds::funcmd_self_test();
    std::printf("funcmd:  %s (%d failures)\n", r ? "FAIL" : "OK", r);
    total += r;

    // 汇总函数也跑一遍(它内部会再调一次各模块, 验证聚合入口不崩)
    nefu::termcmds::termcmds_self_test();

    std::printf("----------------------------------------\n");
    if (total == 0) std::printf("ALL TERMCMDS TESTS PASSED\n");
    else            std::printf("TOTAL FAILURES: %d\n", total);
    return total == 0 ? 0 : 1;
}
