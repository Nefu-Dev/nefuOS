// ============================================================================
// nefuOS 音频DSP库 —— 独立测试主程序
// 编译:
//   g++ -std=c++17 -fno-exceptions -fno-rtti -O2 -I core \
//       tests/audsp_test_main.cpp core/audsp/*.cpp \
//       core/klib/memory.cpp core/klib/string.cpp core/klib/printf.cpp \
//       -o audsp_test.exe
// 运行: 确认所有 self_test 返回 0。
// ============================================================================
#include "../core/audsp/audsp_all.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

// ---- host stubs: klib 需要的 kalloc/kfree/platform_dbg ----
namespace nefu {
void* kalloc(unsigned long long sz) { return std::malloc((size_t)sz); }
void  kfree(void* p) { std::free(p); }
void  platform_dbg(const char* s) { std::fputs(s, stderr); }
}

using namespace nefu::audsp;

int main() {
    printf("=== nefuOS audsp self test ===\n");
    fflush(stdout);

    printf("running osc...\n"); fflush(stdout);
    int f_osc    = osc_self_test();     printf("osc      : %d failures\n", f_osc); fflush(stdout);
    printf("running env...\n"); fflush(stdout);
    int f_env    = env_self_test();     printf("env      : %d failures\n", f_env); fflush(stdout);
    printf("running filter...\n"); fflush(stdout);
    int f_filter = filter_self_test();  printf("filter   : %d failures\n", f_filter); fflush(stdout);
    printf("running effect...\n"); fflush(stdout);
    int f_effect = effect_self_test();  printf("effect   : %d failures\n", f_effect); fflush(stdout);
    printf("running synth...\n"); fflush(stdout);
    int f_synth  = synth_self_test();   printf("synth    : %d failures\n", f_synth); fflush(stdout);
    printf("running analysis...\n"); fflush(stdout);
    int f_analy  = analysis_self_test();printf("analysis : %d failures\n", f_analy); fflush(stdout);
    printf("running midi...\n"); fflush(stdout);
    int f_midi   = midi_self_test();    printf("midi     : %d failures\n", f_midi); fflush(stdout);

    int total = f_osc + f_env + f_filter + f_effect + f_synth + f_analy + f_midi;
    printf("-------------------------------\n");
    if (total == 0) {
        printf("ALL AUDSP TESTS PASSED\n");
        return 0;
    } else {
        printf("FAILURES: %d\n", total);
        return 1;
    }
}
