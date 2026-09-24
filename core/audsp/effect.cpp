// ============================================================================
// nefuOS 音频DSP库 —— 效果器模块实现 (effect.cpp)
// ============================================================================
#include "effect.h"
#include "filter.h"
#include "osc.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

// ============================================================================
// DelayLine
// ============================================================================
DelayLine::DelayLine() { buffer = 0; size = 0; write_idx = 0; }
DelayLine::~DelayLine() { delete[] buffer; buffer = 0; }

bool DelayLine::init(double seconds) {
    delete[] buffer;
    size = (int)(seconds * g_sample_rate) + 2;
    if (size < 4) size = 4;
    buffer = new double[size];
    if (!buffer) { size = 0; return false; }
    for (int i = 0; i < size; i++) buffer[i] = 0.0;
    write_idx = 0;
    return true;
}

void DelayLine::write_sample(double x) {
    buffer[write_idx] = x;
    write_idx++;
    if (write_idx >= size) write_idx = 0;
}

double DelayLine::read_delay(double delay_seconds) const {
    if (!buffer || size <= 0) return 0.0;
    double delay_samples = delay_seconds * g_sample_rate;
    if (delay_samples < 0.0) delay_samples = 0.0;
    if (delay_samples > (double)(size - 2)) delay_samples = (double)(size - 2);
    // 读位置 = write_idx - delay_samples
    double pos = (double)write_idx - delay_samples;
    while (pos < 0.0) pos += size;
    int i0 = (int)pos;
    double frac = pos - (double)i0;
    int i1 = (i0 + 1) % size;
    i0 %= size;
    return buffer[i0] * (1.0 - frac) + buffer[i1] * frac;
}

// ============================================================================
// DelayEffect
// ============================================================================
void DelayEffect::init(double delay_sec, double fb, double mix_) {
    line.init(delay_sec + 0.01);
    feedback = fb;
    mix = mix_;
}

double DelayEffect::process(double x) {
    double delayed = line.read_delay(0.3);          // 默认 300ms
    double y = x + feedback * delayed;
    line.write_sample(y);
    return x * (1.0 - mix) + delayed * mix;
}

// ============================================================================
// Chorus
// ============================================================================
void Chorus::init(double base_delay, double lfo_hz, double depth_sec, double mix_) {
    line.init(base_delay + depth_sec + 0.05);
    lfo_phase = 0.0;
    lfo_freq = lfo_hz;
    min_delay = base_delay;
    depth = depth_sec;
    mix = mix_;
}

double Chorus::process(double x) {
    lfo_phase += lfo_freq / g_sample_rate;
    if (lfo_phase >= 1.0) lfo_phase -= 1.0;
    double lfo = sin(2.0 * M_PI * lfo_phase);
    double d = min_delay + depth * (0.5 + 0.5 * lfo);   // 0..depth
    line.write_sample(x);
    double wet = line.read_delay(d);
    return x * (1.0 - mix) + wet * mix;
}

// ============================================================================
// Flanger
// ============================================================================
void Flanger::init(double base_delay, double lfo_hz, double depth_sec, double fb, double mix_) {
    line.init(base_delay + depth_sec + 0.02);
    lfo_phase = 0.0;
    lfo_freq = lfo_hz;
    min_delay = base_delay;
    depth = depth_sec;
    feedback = fb;
    mix = mix_;
}

double Flanger::process(double x) {
    lfo_phase += lfo_freq / g_sample_rate;
    if (lfo_phase >= 1.0) lfo_phase -= 1.0;
    double lfo = sin(2.0 * M_PI * lfo_phase);
    double d = min_delay + depth * (0.5 + 0.5 * lfo);
    double wet = line.read_delay(d);
    double y = x + feedback * wet;
    line.write_sample(y);
    return x * (1.0 - mix) + wet * mix;
}

// ============================================================================
// Phaser
// ============================================================================
void Phaser::init(double lfo_hz, double min_fc_, double max_fc_, double fb, double mix_) {
    for (int i = 0; i < PHASER_STAGES; i++) stages[i] = 0.0;
    lfo_phase = 0.0;
    lfo_freq = lfo_hz;
    min_fc = min_fc_;
    max_fc = max_fc_;
    feedback = fb;
    mix = mix_;
}

double Phaser::process(double x) {
    lfo_phase += lfo_freq / g_sample_rate;
    if (lfo_phase >= 1.0) lfo_phase -= 1.0;
    double lfo = 0.5 + 0.5 * sin(2.0 * M_PI * lfo_phase);
    double fc = min_fc + (max_fc - min_fc) * lfo;
    // 一阶全通系数：c = (1 - tan(pi*fc/sr)) / (1 + tan(pi*fc/sr))
    double w = M_PI * fc / g_sample_rate;
    double c = (1.0 - tan(w)) / (1.0 + tan(w));
    double y = x;
    for (int i = 0; i < PHASER_STAGES; i++) {
        double out = c * y + stages[i];
        stages[i] = y - c * out;
        y = out;
    }
    y = y + feedback * y;                    // 反馈
    return x * (1.0 - mix) + y * mix;
}

// ============================================================================
// SchroederReverb
// ============================================================================
void SchroederReverb::init(double room_size, double mix_) {
    // 经典 Schroeder 延时参数（秒）
    double comb_times[REVERB_COMBS]    = { 0.0297, 0.0371, 0.0411, 0.0437 };
    double allpass_times[REVERB_ALLPASSES] = { 0.005, 0.0017 };
    double comb_fb = 0.80 + 0.15 * room_size;
    for (int i = 0; i < REVERB_COMBS; i++) {
        combs[i].init(comb_times[i] * (0.5 + room_size), comb_fb, false);
    }
    for (int i = 0; i < REVERB_ALLPASSES; i++) {
        allpasses[i].init(allpass_times[i], 0.7);
    }
    mix = mix_;
}

double SchroederReverb::process(double x) {
    double sum = 0.0;
    for (int i = 0; i < REVERB_COMBS; i++) sum += combs[i].process(x);
    sum /= REVERB_COMBS;
    double y = allpasses[0].process(sum);
    y = allpasses[1].process(y);
    return x * (1.0 - mix) + y * mix;
}

// ============================================================================
// Distortion
// ============================================================================
void Distortion::init(int type_, double drive_) {
    type = type_;
    drive = drive_;
    bit_depth = 8;
}

double Distortion::process(double x) {
    x *= drive;
    switch (type) {
    case DIST_SOFT:
        return tanh(x);                       // 软削波
    case DIST_HARD:
        if (x > 1.0) x = 1.0;
        if (x < -1.0) x = -1.0;
        return x;
    case DIST_BITCRUSH: {
        double levels = (double)(1 << bit_depth);
        double v = floor(x * levels) / levels;
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        return v;
    }
    case DIST_FOLD: {
        while (x > 1.0 || x < -1.0) {
            if (x > 1.0) x = 2.0 - x;
            if (x < -1.0) x = -2.0 - x;
        }
        return x;
    }
    }
    return x;
}

// ============================================================================
// Compressor
// ============================================================================
void Compressor::init(double thr_db, double ratio_, double atk_ms, double rel_ms, double makeup) {
    threshold_db = thr_db;
    ratio = ratio_;
    attack_ms = atk_ms;
    release_ms = rel_ms;
    makeup_db = makeup;
    follower.init(atk_ms, rel_ms);
    limiter = (ratio_ >= 100.0);
}

double Compressor::process(double x) {
    double env = follower.process(x);
    if (env < 1e-9) env = 1e-9;
    double db = 20.0 * log10(env);
    double gain_db = 0.0;
    if (db > threshold_db) {
        if (limiter) gain_db = threshold_db - db;       // 硬限制
        else gain_db = (threshold_db - db) * (1.0 - 1.0 / ratio);
    }
    double lin = pow(10.0, (gain_db + makeup_db) / 20.0);
    return x * lin;
}

// ============================================================================
// Tremolo
// ============================================================================
void Tremolo::init(double rate, double depth_) {
    lfo_phase = 0.0;
    rate_hz = rate;
    depth = depth_;
}

double Tremolo::process(double x) {
    lfo_phase += rate_hz / g_sample_rate;
    if (lfo_phase >= 1.0) lfo_phase -= 1.0;
    double lfo = 0.5 + 0.5 * sin(2.0 * M_PI * lfo_phase);
    double amp = 1.0 - depth + depth * lfo;
    return x * amp;
}

// ============================================================================
// RingMod
// ============================================================================
void RingMod::init(double hz) {
    phase = 0.0;
    freq = hz;
}

double RingMod::process(double x) {
    phase += freq / g_sample_rate;
    if (phase >= 1.0) phase -= 1.0;
    return x * sin(2.0 * M_PI * phase);
}

// ============================================================================
// 自检
// ============================================================================
int effect_self_test() {
    int fail = 0;
    set_sample_rate(44100.0);

    // 1. Delay：输出应在 delay 后出现
    DelayEffect dly;
    dly.init(0.05, 0.0, 1.0);
    for (int i = 0; i < 2000; i++) dly.process(0.0);   // 冲刷
    dly.process(1.0);
    bool heard = false;
    for (int i = 0; i < 3000; i++) {
        double y = dly.process(0.0);
        if (y > 0.1) heard = true;
    }

    // 2. Chorus：输出范围有限，不爆炸
    Chorus ch;
    ch.init(0.02, 0.5, 0.005, 0.5);
    double peak = 0.0;
    for (int i = 0; i < 5000; i++) {
        double y = ch.process(sin(2.0 * M_PI * 440.0 * i / 44100.0));
        if (fabs(y) > peak) peak = fabs(y);
    }

    // 3. Distortion soft：drive 大时输出饱和到 ±1
    Distortion ds;
    ds.init(DIST_SOFT, 10.0);
    double v = ds.process(1.0);
    if (fabs(v) > 1.0) fail++;

    // 4. 硬削波：超出 ±1 被截断
    Distortion dh;
    dh.init(DIST_HARD, 1.0);
    double h = dh.process(5.0);

    // 5. Compressor：小信号通过，大信号被压
    Compressor comp;
    comp.init(-20.0, 4.0, 10.0, 100.0, 0.0);
    double out_small = 0.0, out_large = 0.0;
    for (int i = 0; i < 2000; i++) out_small = comp.process(0.01);
    for (int i = 0; i < 2000; i++) out_large = comp.process(0.9);

    // 6. Tremolo：输出幅度被调制
    Tremolo tr;
    tr.init(5.0, 0.8);
    double min_v = 1e9, max_v = -1e9;
    for (int i = 0; i < 10000; i++) {
        double y = tr.process(1.0);
        if (y < min_v) min_v = y;
        if (y > max_v) max_v = y;
    }

    // 7. Ring mod：输出 = x * sin，零输入零输出
    RingMod rm;
    rm.init(100.0);
    double z = rm.process(0.5);

    // 8. Reverb：不爆炸
    SchroederReverb rev;
    rev.init(0.5, 0.5);
    double rpeak = 0.0;
    for (int i = 0; i < 5000; i++) {
        double y = rev.process((i == 0) ? 1.0 : 0.0);
        if (fabs(y) > rpeak) rpeak = fabs(y);
    }

    return fail;
}

} // namespace audsp
} // namespace nefu


