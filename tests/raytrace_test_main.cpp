// ============================================================================
// nefuOS 光线追踪引擎 —— 独立测试主程序
// ----------------------------------------------------------------------------
// 用法（host 编译，无需 OS 依赖）：
//   g++ -std=c++17 -fno-exceptions -fno-rtti -fno-builtin -O2 -I core ^
//       tests/raytrace_test_main.cpp core/raytrace/*.cpp core/lib/softmath.cpp ^
//       -o raytrace_test.exe
//   raytrace_test.exe
// 退出码 0 = 全部 self_test 通过；非 0 = 失败数。
// 本文件提供 kalloc/kfree/krealloc/platform_dbg 的 host 桩实现。
// ============================================================================
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- host 桩：替代 nefuOS 内核内存分配/调试输出 ----
namespace nefu {
void* kalloc(size_t sz) {
    void* p = std::calloc(1, sz ? sz : 1);
    return p;
}
void kfree(void* p) { std::free(p); }
void* krealloc(void* p, size_t sz) { return std::realloc(p, sz); }
void platform_dbg(const char*) { /* host 下静默 */ }
} // namespace nefu

#include "raytrace/raytrace_all.h"

int main() {
    using namespace nefu::raytrace;
    std::printf("=== nefuOS raytrace engine self test ===\n");

    int t = 0;
    int f;

    f = rtmath_self_test();      std::printf("rtmath     : %d failures\n", f); t += f;
    f = primitives_self_test();  std::printf("primitives : %d failures\n", f); t += f;
    f = materials_self_test();   std::printf("materials  : %d failures\n", f); t += f;
    f = lights_self_test();      std::printf("lights     : %d failures\n", f); t += f;
    f = camera_self_test();      std::printf("camera     : %d failures\n", f); t += f;
    f = texture_self_test();     std::printf("texture    : %d failures\n", f); t += f;
    f = bvh_self_test();         std::printf("bvh        : %d failures\n", f); t += f;
    f = pathtracer_self_test();  std::printf("pathtracer : %d failures\n", f); t += f;
    f = scene_self_test();       std::printf("scene      : %d failures\n", f); t += f;
    f = rtutil_self_test();      std::printf("rtutil     : %d failures\n", f); t += f;
    f = rtmesh_self_test();      std::printf("rtmesh     : %d failures\n", f); t += f;
    f = rtsampler_self_test();    std::printf("rtsampler  : %d failures\n", f); t += f;
    f = rtaccel_self_test();      std::printf("rtaccel    : %d failures\n", f); t += f;
    f = rtonemap_self_test();    std::printf("rtonemap   : %d failures\n", f); t += f;
    f = rtbrdf_self_test();      std::printf("rtbrdf     : %d failures\n", f); t += f;
    f = rtnoise_self_test();      std::printf("rtnoise    : %d failures\n", f); t += f;
    f = rtintegrator_self_test(); std::printf("rtintegrator: %d failures\n", f); t += f;
    f = rtserialize_self_test(); std::printf("rtserialize: %d failures\n", f); t += f;
    f = rtcampath_self_test();   std::printf("rtcampath  : %d failures\n", f); t += f;
    f = rtstats_self_test();     std::printf("rtstats    : %d failures\n", f); t += f;
    f = rtenv_self_test();       std::printf("rtenv      : %d failures\n", f); t += f;
    f = rtbake_self_test();       std::printf("rtbake     : %d failures\n", f); t += f;
    f = rtfilter_self_test();     std::printf("rtfilter   : %d failures\n", f); t += f;
    f = rtlightprobe_self_test(); std::printf("rtlightprb : %d failures\n", f); t += f;
    f = rtmis_self_test();        std::printf("rtmis      : %d failures\n", f); t += f;
    f = rtinstance_self_test();   std::printf("rtinstance : %d failures\n", f); t += f;
    f = rtlighttree_self_test(); std::printf("rtlighttree: %d failures\n", f); t += f;
    f = rtraypool_self_test();    std::printf("rtraypool  : %d failures\n", f); t += f;
    f = rtaccum_self_test();      std::printf("rtaccum    : %d failures\n", f); t += f;
    f = rtrender_self_test();     std::printf("rtrender   : %d failures\n", f); t += f;
    f = rtshader_self_test();      std::printf("rtshader   : %d failures\n", f); t += f;
    f = rtmesh2_self_test();       std::printf("rtmesh2    : %d failures\n", f); t += f;
    f = rtworld_self_test();       std::printf("rtworld    : %d failures\n", f); t += f;
    f = rtsky_self_test();         std::printf("rtsky      : %d failures\n", f); t += f;
    f = rtpost_self_test();        std::printf("rtpost     : %d failures\n", f); t += f;

    std::printf("----------------------------------------\n");
    std::printf("TOTAL failures: %d\n", t);
    std::printf(t == 0 ? "ALL SELF TESTS PASSED\n" : "SOME TESTS FAILED\n");
    return t ? 1 : 0;
}
