// nefuOS 文件系统库 —— 独立宿主测试 main(桩)
//
// 用法(在 nefuOS 根目录下):
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 ^
//     -I core tests\filesystem_test_main.cpp ^
//     core\filesystem\*.cpp core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp ^
//     -o %TEMP%\filesystem_test.exe
//
// 本文件自带 kalloc/kfree/krealloc/platform_dbg 的宿主桩,
// 不依赖任何 OS / VFS / GUI 代码,可单独编译跑全部 self_test。
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ---- 宿主内存桩 ----
namespace nefu {
void* kalloc(size_t sz) { return malloc(sz ? sz : 1); }
void kfree(void* p) { free(p); }
void* krealloc(void* p, size_t sz) { return realloc(p, sz ? sz : 1); }
void platform_dbg(const char* s) { fputs(s, stdout); fputc('\n', stdout); }
} // namespace nefu

#include "filesystem/filesystem_all.h"
using namespace nefu;
using namespace nefu::filesystem;

int main() {
    int d = disk_self_test();
    int f = fat16_self_test();
    int e = ext2_self_test();
    int m = minixfs_self_test();
    int v = vfs_layer_self_test();
    int j = journal_self_test();
    int c = cache_self_test();
    int sm = filesystem_integration_smoke();
    int all = filesystem_self_test() + sm;

    printf("disk self_test     = %d failures\n", d);
    printf("fat16 self_test    = %d failures\n", f);
    printf("ext2 self_test     = %d failures\n", e);
    printf("minixfs self_test  = %d failures\n", m);
    printf("vfs self_test      = %d failures\n", v);
    printf("journal self_test  = %d failures\n", j);
    printf("cache self_test    = %d failures\n", c);
    printf("integration smoke  = %d failures\n", sm);
    printf("--------------------------------\n");
    printf("TOTAL filesystem   = %d failures\n", all);

    if (all == 0) { printf("\nALL FILESYSTEM TESTS PASSED\n"); return 0; }
    printf("\nFILESYSTEM TESTS FAILED\n");
    return 1;
}
