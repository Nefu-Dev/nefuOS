// nefuOS UI 组件库 —— 宿主独立编译测试入口
// 编译:
//   g++.exe -std=c++17 -fno-exceptions -fno-rtti -O2 -I core \
//     tests/uiwidgets_test_main.cpp core/uiwidgets/*.cpp core/gfxlib/*.cpp \
//     core/klib/memory.cpp core/klib/string.cpp core/klib/printf.cpp \
//     -o %TEMP%\uiwidgets_test.exe
// 运行:退出码 0 = 全部自检通过。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- klib 需要的平台桩(宿主用 malloc/free/printf 实现) ----
#include "platform.h"

namespace nefu {

void* kalloc(size_t sz) {
    if (sz == 0) sz = 1;
    return malloc(sz);
}
void kfree(void* p) {
    free(p);
}
void* krealloc(void* p, size_t sz) {
    return realloc(p, sz);
}
void platform_dbg(const char* s) {
    fputs(s, stdout);
}
uint32_t platform_tick_ms() {
    return 0;
}

} // namespace nefu

// ---- 被测库 ----
#include "uiwidgets/uiwidgets_all.h"

int main() {
    int f = 0;
    int rf = nefu::ui::widget_self_test();
    int cf = nefu::ui::controls_self_test();
    int tf = nefu::ui::tableview_self_test();
    int chf = nefu::ui::chart_self_test();
    int df = nefu::ui::dialog_self_test();
    int mf = nefu::ui::menu_self_test();
    int all = nefu::ui::uiwidgets_self_test();

    printf("uiwidgets self test:\n");
    printf("  widget    : %d failures\n", rf);
    printf("  controls  : %d failures\n", cf);
    printf("  tableview : %d failures\n", tf);
    printf("  chart     : %d failures\n", chf);
    printf("  dialog    : %d failures\n", df);
    printf("  menu      : %d failures\n", mf);
    printf("  TOTAL     : %d failures\n", all);

    f = all;
    if (f == 0) printf("ALL UIWIDGETS TESTS PASSED\n");
    else        printf("UIWIDGETS FAILURES DETECTED\n");
    return f == 0 ? 0 : 1;
}
