// nefuOS 压缩算法库 —— 独立编译自测主程序
//
// 用指定命令编译：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -O2 -I core \
//       tests\compress_test_main.cpp core\compress\*.cpp \
//       core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp \
//       -o compress_test.exe
//
// 这里自己提供 kalloc/kfree/krealloc/platform_dbg 的桩，
// 使压缩库能脱离操作系统内核单独在宿主机上跑。
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

namespace nefu {
void* kalloc(size_t s) { return malloc(s ? s : 1); }
void  kfree(void* p) { free(p); }
void* krealloc(void* p, size_t s) { return realloc(p, s); }
void platform_dbg(const char* s) { (void)s; }
}

#include "compress/compress_all.h"

int main() {
    int f = nefu::compress::compress_self_test();
    printf("compress library self_test: %d failures\n", f);
    return f != 0;
}
