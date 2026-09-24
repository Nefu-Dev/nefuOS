// nefuOS audlib —— 调制 modulate 与移调 pitch
// 教学版：LFO/颤音/滑音调制器 + 半音移调（重采样）。
#pragma once
#include <cmath>
#include "audlib/wave.h"

namespace nefu {
namespace audx {

// 调制器：LFO（低频振荡）+ 颤音 + 滑音
class Modulator {
public:
    Modulator() : lfo_rate(5.0), lfo_depth(0.0), vibrato_rate(5.0), vibrato_depth(0.0) {}

    // 设置 LFO：速率（Hz）、深度（0..1 幅度调制）
    void set_lfo(double rate, double depth);
    // 设置颤音：速率、深度（0..0.2 音高调制）
    void set_vibrato(double rate, double depth);

    // 生成调制后的音调：载波 freq 上加颤音、幅度上加 LFO
    AudioBuf modulate(const AudioBuf& carrier, double freq) const;

    // 应用滑音：从 f0 滑到 f1 的扫频音
    AudioBuf glide(double seconds, double f0, double f1, int wave_type, double amp) const;

    // ---- self test ----
    static int self_test();

private:
    double lfo_rate, lfo_depth, vibrato_rate, vibrato_depth;
};

// 移调器：按半音数移调（线性重采样近似）
class Pitch {
public:
    // 移调 semitones 个半音（正=升高），返回新音频
    static AudioBuf shift(const AudioBuf& in, int semitones);
    // 变速（不改变音高）：速度倍率 rate（>1 变快）
    static AudioBuf time_stretch(const AudioBuf& in, double rate);

    // ---- self test ----
    static int self_test();
};

} // namespace audx
} // namespace nefu
