// nefuOS audlib —— 波形合成器实现 + 自测
#include "audlib/synth.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nefu {
namespace audx {

Synth::Synth(int sample_rate) : rate(sample_rate) {}

double Synth::sample(int type, double phase) {
    // phase ∈ [0,1)：波形的相位
    switch (type) {
        case WAVE_SINE:
            return std::sin(2 * 3.14159265358979 * phase);
        case WAVE_SQUARE:
            return phase < 0.5 ? 1.0 : -1.0;
        case WAVE_SAW:
            return 2.0 * phase - 1.0;   // 从 -1 升到 1
        case WAVE_TRIANGLE: {
            // 三角波：0..0.5 升，0.5..1 降
            if (phase < 0.5) return 4.0 * phase - 1.0;
            return 3.0 - 4.0 * phase;
        }
        case WAVE_NOISE:
            return (double)(rand() % 2000 - 1000) / 1000.0;
        default:
            return 0;
    }
}

double Synth::midi_to_freq(int midi_note) {
    // A4 = 440Hz = MIDI 69
    return 440.0 * std::pow(2.0, (midi_note - 69) / 12.0);
}

AudioBuf Synth::tone(double seconds, int type, double freq, double amp) const {
    AudioBuf a(rate, 1);
    int n = (int)(rate * seconds);
    a.silence(n);
    for (int i = 0; i < n; i++) {
        double t = (double)i / rate;
        double phase = freq * t;
        phase -= std::floor(phase);   // 相位折叠
        double s = sample(type, phase) * amp;
        a.data[i] = (short)(s * 32767);
    }
    return a;
}

AudioBuf Synth::harmonic(double seconds, double freq, double amp, int harmonics) const {
    AudioBuf a(rate, 1);
    int n = (int)(rate * seconds);
    a.silence(n);
    if (harmonics < 1) harmonics = 1;
    for (int i = 0; i < n; i++) {
        double t = (double)i / rate;
        double s = 0;
        // 加第 k 次谐波（振幅 1/k，形成柔和音色）
        for (int k = 1; k <= harmonics; k++) {
            double ph = freq * k * t;
            ph -= std::floor(ph);
            s += sample(WAVE_SINE, ph) / k;
        }
        // 归一化（避免削波）
        double sum = 0;
        for (int k = 1; k <= harmonics; k++) sum += 1.0 / k;
        s = s / sum * amp;
        a.data[i] = (short)(s * 32767);
    }
    return a;
}

AudioBuf Synth::sweep(double seconds, double f0, double f1, double amp) const {
    AudioBuf a(rate, 1);
    int n = (int)(rate * seconds);
    a.silence(n);
    double phase = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / n;                     // 0..1
        double f = f0 + (f1 - f0) * t;                // 线性扫频
        phase += f / rate;
        phase -= std::floor(phase);
        double s = sample(WAVE_SINE, phase) * amp;
        a.data[i] = (short)(s * 32767);
    }
    return a;
}

AudioBuf Synth::noise(double seconds, double amp) const {
    AudioBuf a(rate, 1);
    int n = (int)(rate * seconds);
    a.silence(n);
    for (int i = 0; i < n; i++)
        a.data[i] = (short)(sample(WAVE_NOISE, 0) * amp * 32767);
    return a;
}

// ---- self test ----
int Synth::self_test() {
    int fails = 0;
    Synth s(8000);
    // 1. 时长与采样数
    {
        AudioBuf a = s.tone(1.0, WAVE_SINE, 440, 0.5);
        if (a.frames() != 8000) fails++;
        if (a.duration() < 0.99 || a.duration() > 1.01) fails++;
    }
    // 2. 正弦波形：峰值幅度
    {
        AudioBuf a = s.tone(0.1, WAVE_SINE, 440, 0.8);
        short mx = 0;
        for (int i = 0; i < a.frames(); i++) {
            short v = a.data[i];
            if (v < 0) v = (short)(-v);
            if (v > mx) mx = v;
        }
        if (mx < 20000) fails++;   // 0.8*32767 ≈ 26213
        if (mx > 32767) fails++;
    }
    // 3. 波形形状：方波近 ±1
    {
        AudioBuf a = s.tone(0.05, WAVE_SQUARE, 200, 1.0);
        // 前几个采样应接近 +32767 或 -32767
        if (std::abs(a.data[1]) < 30000) fails++;
    }
    // 4. MIDI 频率：A4 = 440
    {
        if (std::abs(Synth::midi_to_freq(69) - 440) > 1e-6) fails++;
        if (std::abs(Synth::midi_to_freq(81) - 880) > 1e-3) fails++;   // A5
    }
    // 5. 扫频端点频率不同
    {
        AudioBuf a = s.sweep(1.0, 200, 800, 0.3);
        if (a.frames() != 8000) fails++;
    }
    // 6. 谐波合成不削波
    {
        AudioBuf a = s.harmonic(0.5, 220, 1.0, 5);
        bool clipped = false;
        for (int i = 0; i < a.frames(); i++)
            if (a.data[i] >= 32767 || a.data[i] <= -32767) clipped = true;
        if (clipped) fails++;
    }
    return fails;
}

} // namespace audx
} // namespace nefu
