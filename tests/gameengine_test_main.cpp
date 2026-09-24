// gameengine_test_main.cpp —— 2D 游戏引擎宿主机独立自测
//
// 编译命令（在 nefuOS 根目录）：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 -I core ^
//       tests\gameengine_test_main.cpp ^
//       core\gameengine\ge_math.cpp core\gameengine\sprite.cpp ^
//       core\gameengine\tilemap.cpp core\gameengine\physics2d.cpp ^
//       core\gameengine\scene.cpp core\gameengine\input.cpp ^
//       core\gameengine\audio_engine.cpp core\gameengine\particle_engine.cpp ^
//       core\gameengine\ui.cpp core\lib\softmath.cpp ^
//       -o gameengine_test.exe
//
// 本文件自己提供 kalloc/kfree/krealloc/platform_dbg 桩，
// 以及全局 operator new/delete（路由到 kalloc），
// 让各 gameengine .cpp 脱离内核与 gfx 单独在宿主机运行。
//
// 返回 0 表示全部通过；非 0 表示失败数。
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

// ---- 内核桩：nefu 命名空间 ----
namespace nefu {
void* kalloc(size_t s) { return malloc(s ? s : 1); }
void  kfree(void* p) { free(p); }
void* krealloc(void* p, size_t s) { return realloc(p, s); }
void platform_dbg(const char* s) { (void)s; }
}

// ---- C++ 全局 new/delete：路由到 kalloc ----
// nefu::List<T> 内部用 new T[]，必须提供。
void* operator new(size_t sz)     { return nefu::kalloc(sz); }
void* operator new[](size_t sz)  { return nefu::kalloc(sz); }
void  operator delete(void* p)    noexcept { nefu::kfree(p); }
void  operator delete[](void* p)  noexcept { nefu::kfree(p); }
void  operator delete(void* p, size_t) noexcept { nefu::kfree(p); }
void  operator delete[](void* p, size_t) noexcept { nefu::kfree(p); }

// ---- 聚合头：include 全部模块 ----
#include "../core/gameengine/gameengine_all.h"

using namespace nefu;

int main() {
    printf("gameengine self test starting...\n");
    int f = gameengine::gameengine_self_test();
    printf("----\n");
    printf("gameengine self test: %d failures\n", f);
    return f != 0;
}
