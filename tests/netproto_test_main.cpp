// nefuOS 网络协议栈独立测试主程序
// 编译（在 nefuOS 根目录下）：
//   D:\CLion\bin\mingw\bin\g++.exe -std=c++17 -fno-exceptions -fno-rtti
//     -fno-builtin -O2 -I core
//     tests\netproto_test_main.cpp
//     core\netproto\*.cpp
//     core\klib\memory.cpp core\klib\string.cpp core\klib\printf.cpp
//     -o netproto_test.exe
//
// 运行：netproto_test.exe   —— 全部通过返回 0。
#include "netproto/netproto_all.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 宿主桩：klib/memory.cpp 的 operator new 依赖 kalloc/kfree，printf.cpp 依赖 platform_dbg。
// 裸机/宿主正式构建由平台层提供；这里用标准库补齐，便于独立编译验证。
namespace nefu {
void* kalloc(size_t n) { return std::malloc(n ? n : 1); }
void  kfree(void* p) { std::free(p); }
void  platform_dbg(const char*) {}
} // namespace nefu

using namespace nefu::netproto;

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int fails = netproto_self_test();
    printf("\n=== netproto RESULT: %s (failures=%d) ===\n",
           fails == 0 ? "ALL PASS" : "SOME FAILURES", fails);
    return fails == 0 ? 0 : 1;
}
