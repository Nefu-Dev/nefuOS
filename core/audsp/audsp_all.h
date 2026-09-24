// ============================================================================
// nefuOS 音频DSP库 —— 聚合头 (audsp_all.h)
// ----------------------------------------------------------------------------
// 包含所有子模块头，并声明 audsp_self_test() 汇总调用各模块自检。
// ============================================================================
#pragma once

#include "osc.h"
#include "env.h"
#include "filter.h"
#include "effect.h"
#include "synth.h"
#include "analysis.h"
#include "midi.h"

namespace nefu {
namespace audsp {

// ---- 汇总自检：返回总失败数 (0 = 全部通过) ----
inline int audsp_self_test() {
    int fail = 0;
    fail += osc_self_test();
    fail += env_self_test();
    fail += filter_self_test();
    fail += effect_self_test();
    fail += synth_self_test();
    fail += analysis_self_test();
    fail += midi_self_test();
    return fail;
}

} // namespace audsp
} // namespace nefu
