// nefuOS audlib —— 波形合成器 synth
// 教学版：生成正弦/方波/锯齿/三角波，支持频率、振幅、相位。
// 用于电子音乐与合成器教学。class / STL / cmath / 中文注释。
#pragma once
#include <vector>
#include "audlib/wave.h"

namespace nefu {
namespace audx {

// 波形类型
enum WaveType {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_TRIANGLE,
    WAVE_NOISE
};

// 波形合成器：按采样率合成一段音频
class Synth {
public:
    // 构造：采样率
    Synth(int sample_rate);

    // 生成 n 秒音频：类型、频率、振幅（0..1）
    // 返回 AudioBuf（单声道）
    AudioBuf tone(double seconds, int type, double freq, double amp) const;
    // 生成含谐波的正弦音（谐波数 harmonics，可加甜味）
    AudioBuf harmonic(double seconds, double freq, double amp, int harmonics) const;
    // 生成扫频（chirp）：频率从 f0 到 f1
    AudioBuf sweep(double seconds, double f0, double f1, double amp) const;
    // 生成噪声
    AudioBuf noise(double seconds, double amp) const;

    // 静态：单个采样（相位 0..1，返回 -1..1）
    static double sample(int type, double phase);
    // 静态：频率/音名工具
    static double midi_to_freq(int midi_note);   // MIDI 音符 -> 频率

    int sample_rate() const { return rate; }

    // ---- self test ----
    static int self_test();

private:
    int rate;
};

} // namespace audx
} // namespace nefu
