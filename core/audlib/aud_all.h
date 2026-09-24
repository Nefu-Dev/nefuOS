// nefuOS 音频库 audlib —— STL 风格聚合头
// 一次 include 全部音频模块。自测：aud_all_self_test() 汇总各模块失败数。
// 教学版：全部用 class / do-while / switch / std 模板 / cmath 实现，中文注释。
#pragma once

#include "audlib/wave.h"
#include "audlib/synth.h"
#include "audlib/env.h"
#include "audlib/filter.h"
#include "audlib/seq.h"
#include "audlib/mixer.h"
#include "audlib/modulate.h"

namespace nefu {
namespace audx {

// 汇总自测：返回失败总数，0 表示全部通过
inline int aud_all_self_test() {
    return WaveIO::self_test() + Synth::self_test() +
           Envelope::self_test() + BiquadFilter::self_test() +
           LowPass1::self_test() + Note::self_test() +
           Sequencer::self_test() + Mixer::self_test() +
           Delay::self_test() + Reverb::self_test() +
           Modulator::self_test() + Pitch::self_test();
}

} // namespace audx
} // namespace nefu
