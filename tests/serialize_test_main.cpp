// 独立宿主测试主程序：运行 core/serialize 所有模块的 self_test。
// 编译见任务说明（见 tests/build 说明）。这里只提供 kalloc/kfree/platform_dbg 桩。
#include "../core/serialize/serialize_all.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nefu {
// 宿主桩：kalloc/kfree 用 malloc/free 实现（裸机里由后端提供）
void* kalloc(size_t sz) { return std::malloc(sz ? sz : 1); }
void  kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void  platform_dbg(const char* s) { fputs(s, stderr); }
} // namespace nefu

int main() {
    int f = nefu::serialize::serialize_self_test();
    printf("serialize self_test failures = %d\n", f);
    return f ? 1 : 0;
}
// 独立编译入口：桩实现 kalloc/kfree/krealloc/platform_dbg，然后调用 serialize_self_test()。
// 编译命令见任务说明，全部 self_test 返回 0 即通过。