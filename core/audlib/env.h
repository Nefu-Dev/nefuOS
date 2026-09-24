// nefuOS audlib —— 包络 env（ADSR）
// 教学版：Attack-Decay-Sustain-Release 包络发生器，控制音量随时间变化。
// 用于乐器音色的起音/衰减/保持/释音阶段。class / 中文注释。
#pragma once
#include <vector>
#include "audlib/wave.h"

namespace nefu {
namespace audx {

// ADSR 包络
class Envelope {
public:
    // 时间参数（秒）
    double a_time, d_time, s_level, r_time;

    Envelope() : a_time(0.01), d_time(0.1), s_level(0.7), r_time(0.2) {}

    // 计算 t 时刻的包络值（0..1）；note_dur 为音符总时长
    // 流程：起音 a -> 衰减 d -> 保持 s -> 释音 r
    double value(double t, double note_dur) const;

    // 生成包络曲线（按采样率，n 帧，note_dur 秒）
    std::vector<double> curve(int sample_rate, double note_dur) const;

    // 应用到音频缓冲（逐帧乘包络）
    void apply(AudioBuf& a, double note_dur) const;

    // ---- self test ----
    static int self_test();
};

} // namespace audx
} // namespace nefu
