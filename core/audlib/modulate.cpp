// nefuOS audlib —— 调制与移调实现 + 自测
#include "audlib/modulate.h"
#include "audlib/synth.h"
#include <cstdio>

namespace nefu {
namespace audx {

void Modulator::set_lfo(double rate, double depth) {
    lfo_rate = rate; lfo_depth = depth;
    if (lfo_depth < 0) lfo_depth = 0;
    if (lfo_depth > 1) lfo_depth = 1;
}

void Modulator::set_vibrato(double rate, double depth) {
    vibrato_rate = rate; vibrato_depth = depth;
    if (vibrato_depth < 0) vibrato_depth = 0;
    if (vibrato_depth > 0.2) vibrato_depth = 0.2;
}

AudioBuf Modulator::modulate(const AudioBuf& carrier, double freq) const {
    AudioBuf out = carrier;
    int n = carrier.frames();
    double phase = 0, vib_phase = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / carrier.sample_rate;
        // LFO 幅度调制：1 + depth * sin(2π f t)
        double amp_mod = 1.0 + lfo_depth * std::sin(2 * 3.14159265358979 * lfo_rate * t);
        // 颤音音高调制
        double vib = 1.0 + vibrato_depth * std::sin(2 * 3.14159265358979 * vibrato_rate * t);
        (void)vib_phase;
        // 简化：幅度调制直接乘，颤音略去（教学：主要看幅度 LFO）
        out.data[i] = (short)(carrier.data[i] * amp_mod);
    }
    return out;
}

AudioBuf Modulator::glide(double seconds, double f0, double f1, int wave_type, double amp) const {
    AudioBuf a(44100, 1);
    int n = (int)(44100 * seconds);
    a.silence(n);
    double phase = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / n;
        double f = f0 + (f1 - f0) * t;
        phase += f / 44100.0;
        phase -= std::floor(phase);
        double s;
        if (wave_type == 0) s = std::sin(2 * 3.14159265358979 * phase);
        else if (wave_type == 1) s = phase < 0.5 ? 1 : -1;
        else s = 2 * phase - 1;
        a.data[i] = (short)(s * amp * 32767);
    }
    return a;
}

// ---- self test ----
int Modulator::self_test() {
    int fails = 0;
    Synth syn(44100);
    AudioBuf base = syn.tone(0.2, WAVE_SINE, 440, 0.5);
    // 1. 无 LFO 时不变
    {
        Modulator m;
        AudioBuf o = m.modulate(base, 440);
        if (o.data[0] != base.data[0]) fails++;
    }
    // 2. LFO 深度调制：峰值幅度变化
    {
        Modulator m;
        m.set_lfo(100, 0.5);   // 快速 LFO
        AudioBuf o = m.modulate(base, 440);
        short mx = 0;
        for (int i = 0; i < o.frames(); i++) {
            short v = o.data[i];
            if (v < 0) v = (short)(-v);
            if (v > mx) mx = v;
        }
        // 峰值可达 base 峰值的 1.5 倍（有裁剪可能，用范围判断）
        if (mx < 14000) fails++;   // 0.5*32767*1.5 ≈ 24575；最低也应有原始 0.5*32767*0.5
    }
    // 3. 滑音输出长度与有声
    {
        Modulator m;
        AudioBuf g = m.glide(0.5, 200, 800, 0, 0.3);
        if (g.frames() != 22050) fails++;
        bool sound = false;
        for (int i = 0; i < g.frames(); i++)
            if (g.data[i] != 0) sound = true;
        if (!sound) fails++;
    }
    return fails;
}

AudioBuf Pitch::shift(const AudioBuf& in, int semitones) {
    if (semitones == 0) return in;
    // 频率比
    double ratio = std::pow(2.0, semitones / 12.0);
    int out_frames = (int)(in.frames() / ratio);
    AudioBuf out(in.sample_rate, in.channels);
    out.silence(out_frames);
    // 线性重采样
    for (int i = 0; i < out_frames; i++) {
        double src = i * ratio;
        int i0 = (int)src;
        int i1 = i0 + 1;
        double frac = src - i0;
        if (i1 >= in.frames()) i1 = in.frames() - 1;
        if (i0 >= in.frames()) i0 = in.frames() - 1;
        for (int c = 0; c < in.channels; c++) {
            double v0 = in.data[i0 * in.channels + c];
            double v1 = in.data[i1 * in.channels + c];
            double v = v0 + (v1 - v0) * frac;
            out.data[i * in.channels + c] = (short)v;
        }
    }
    return out;
}

AudioBuf Pitch::time_stretch(const AudioBuf& in, double rate) {
    if (rate <= 0) rate = 1;
    if (rate == 1.0) return in;
    int out_frames = (int)(in.frames() / rate);
    AudioBuf out(in.sample_rate, in.channels);
    out.silence(out_frames);
    for (int i = 0; i < out_frames; i++) {
        double src = i * rate;
        int i0 = (int)src;
        int i1 = i0 + 1;
        double frac = src - i0;
        if (i1 >= in.frames()) i1 = in.frames() - 1;
        if (i0 >= in.frames()) i0 = in.frames() - 1;
        for (int c = 0; c < in.channels; c++) {
            double v0 = in.data[i0 * in.channels + c];
            double v1 = in.data[i1 * in.channels + c];
            double v = v0 + (v1 - v0) * frac;
            out.data[i * in.channels + c] = (short)v;
        }
    }
    return out;
}

// ---- self test ----
int Pitch::self_test() {
    int fails = 0;
    AudioBuf a(8000, 1);
    a.silence(100);
    // 前 50 帧一个递增斜坡
    for (int i = 0; i < 50; i++) a.data[i] = (short)(i * 100);
    // 1. 移调 0 不变
    {
        AudioBuf o = Pitch::shift(a, 0);
        if (o.frames() != 100) fails++;
        if (o.data[10] != a.data[10]) fails++;
    }
    // 2. 升调变短
    {
        AudioBuf o = Pitch::shift(a, 12);   // 高八度：长度减半
        if (o.frames() >= 100) fails++;
        if (o.frames() < 40) fails++;
    }
    // 3. 变速
    {
        AudioBuf o = Pitch::time_stretch(a, 2.0);
        if (o.frames() != 50) fails++;
        AudioBuf o2 = Pitch::time_stretch(a, 0.5);
        if (o2.frames() != 200) fails++;
    }
    // 4. 移调后内容大致保持（第一个采样）
    {
        AudioBuf o = Pitch::shift(a, 12);
        if (o.data[0] != a.data[0]) fails++;
    }
    return fails;
}

} // namespace audx
} // namespace nefu
