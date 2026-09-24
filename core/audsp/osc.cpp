// ============================================================================
// nefuOS 音频DSP库 —— 振荡器模块实现 (osc.cpp)
// ============================================================================
#include "osc.h"
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

// ---- 全局采样率 ----
double g_sample_rate = 44100.0;
void set_sample_rate(double sr) { if (sr > 1.0) g_sample_rate = sr; }

// ---- 波形名表 ----
const char* waveform_name(int w) {
    switch (w) {
    case WAVE_SINE:      return "sine";
    case WAVE_SQUARE:    return "square";
    case WAVE_SAWTOOTH:  return "sawtooth";
    case WAVE_TRIANGLE:  return "triangle";
    case WAVE_PULSE:     return "pulse";
    case WAVE_NOISE_WHITE: return "noise-white";
    case WAVE_NOISE_PINK:  return "noise-pink";
    case WAVE_NOISE_BROWN: return "noise-brown";
    default: return "?";
    }
}

// ============================================================================
// 正弦查表
// ============================================================================
static double g_sine_table[SINE_TABLE_LEN];
static bool   g_sine_ready = false;

void sine_table_init() {
    if (g_sine_ready) return;
    for (int i = 0; i < SINE_TABLE_LEN; i++) {
        g_sine_table[i] = sin(2.0 * M_PI * (double)i / (double)SINE_TABLE_LEN);
    }
    g_sine_ready = true;
}

double sine_lookup(double phase) {
    if (!g_sine_ready) sine_table_init();
    if (phase < 0.0) phase += 1.0;
    phase -= floor(phase);                       // 归一到 [0,1)
    double pos = phase * (double)SINE_TABLE_LEN;
    int idx = (int)pos;
    double frac = pos - (double)idx;
    int i0 = idx & (SINE_TABLE_LEN - 1);
    int i1 = (idx + 1) & (SINE_TABLE_LEN - 1);
    double v0 = g_sine_table[i0];
    double v1 = g_sine_table[i1];
    return v0 + (v1 - v0) * frac;                // 线性插值
}

// ============================================================================
// Oscillator
// ============================================================================
void Oscillator::init() {
    phase = 0.0;
    freq  = 440.0;
    duty  = 0.5;
    wave  = WAVE_SINE;
    noise_state = 0.0;
    brown_state = 0.0;
    lcg   = 0x12345678u;
}

void Oscillator::set_frequency(double hz) {
    if (hz < 0.0) hz = 0.0;
    if (hz > g_sample_rate * 0.49) hz = g_sample_rate * 0.49;  // 抗混叠上限
    freq = hz;
}

void Oscillator::set_waveform(int w) {
    if (w >= 0 && w < WAVE_COUNT) wave = w;
}

void Oscillator::set_duty(double d) {
    if (d < 0.01) d = 0.01;
    if (d > 0.99) d = 0.99;
    duty = d;
}

double Oscillator::process() {
    double inc = freq / g_sample_rate;           // 每采样相位增量
    phase += inc;
    if (phase >= 1.0) phase -= 1.0;

    switch (wave) {
    case WAVE_SINE:
        return sine_lookup(phase);
    case WAVE_SQUARE:
        return (phase < 0.5) ? 1.0 : -1.0;
    case WAVE_SAWTOOTH:
        return 2.0 * phase - 1.0;                 // 上升锯齿
    case WAVE_TRIANGLE: {
        // 三角波：在 [0,0.5] 上升，[0.5,1] 下降
        if (phase < 0.5) return 4.0 * phase - 1.0;
        else             return 3.0 - 4.0 * phase;
    }
    case WAVE_PULSE:
        return (phase < duty) ? 1.0 : -1.0;
    case WAVE_NOISE_WHITE: {
        lcg = lcg * 1664525u + 1013904223u;
        return ((double)(lcg >> 8) / 16777216.0) * 2.0 - 1.0;
    }
    case WAVE_NOISE_PINK: {
        // Paul Kellet 一阶 pink 近似
        lcg = lcg * 1664525u + 1013904223u;
        double white = ((double)(lcg >> 8) / 16777216.0) * 2.0 - 1.0;
        noise_state = 0.997 * noise_state + 0.029591 * white;
        return noise_state * 3.5;
    }
    case WAVE_NOISE_BROWN: {
        lcg = lcg * 1664525u + 1013904223u;
        double white = ((double)(lcg >> 8) / 16777216.0) * 2.0 - 1.0;
        brown_state += 0.02 * white;
        if (brown_state > 1.0) brown_state = 1.0;
        if (brown_state < -1.0) brown_state = -1.0;
        return brown_state * 3.5;
    }
    default:
        return 0.0;
    }
}

void Oscillator::process_block(double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process();
}

// ============================================================================
// FmOperator
// ============================================================================
void FmOperator::init() {
    phase = 0.0;
    freq  = 440.0;
    mod_depth = 0.0;
    output_level = 1.0;
}

void FmOperator::set_frequency(double hz) {
    if (hz < 0.0) hz = 0.0;
    freq = hz;
}

void FmOperator::set_mod_depth(double d) { mod_depth = d; }
void FmOperator::set_output_level(double l) { output_level = l; }

double FmOperator::process(double mod_input) {
    // 相位增量 = 基频 + 调制量（mod_depth * mod_input 作为频率偏移 Hz）
    double effective = freq + mod_depth * mod_input;
    if (effective < 0.0) effective = 0.0;
    phase += effective / g_sample_rate;
    if (phase >= 1.0) phase -= 1.0;
    if (phase < 0.0) phase += 1.0;
    return sine_lookup(phase) * output_level;
}

void FmOperator::process_block(const double* mod_in, double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process(mod_in[i]);
}

// ============================================================================
// WavetableOsc
// ============================================================================
WavetableOsc::WavetableOsc() {
    table = 0;
    table_len = 0;
    phase = 0.0;
    freq = 440.0;
}

WavetableOsc::~WavetableOsc() {
    delete[] table;
    table = 0;
}

bool WavetableOsc::load_table(const double* data, int len) {
    if (!data || len <= 0) return false;
    delete[] table;
    table = new double[len];
    if (!table) { table_len = 0; return false; }
    for (int i = 0; i < len; i++) table[i] = data[i];
    table_len = len;
    return true;
}

void WavetableOsc::set_frequency(double hz) {
    if (hz < 0.0) hz = 0.0;
    freq = hz;
}

double WavetableOsc::process() {
    if (!table || table_len <= 0) return 0.0;
    phase += freq / g_sample_rate;
    if (phase >= 1.0) phase -= 1.0;
    double pos = phase * (double)table_len;
    int i0 = (int)pos;
    double frac = pos - (double)i0;
    int i1 = (i0 + 1) % table_len;
    if (i0 >= table_len) i0 = table_len - 1;
    return table[i0] + (table[i1] - table[i0]) * frac;
}

void WavetableOsc::process_block(double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process();
}

// ============================================================================
// NoiseGenerator
// ============================================================================
void NoiseGenerator::init(int t) {
    type = t;
    lcg = 0xA53F1234u;
    pink_b0 = pink_b1 = pink_b2 = pink_b3 = pink_b4 = pink_b5 = pink_b6 = 0.0;
    brown_last = 0.0;
}

double NoiseGenerator::process() {
    lcg = lcg * 1664525u + 1013904223u;
    double white = ((double)(lcg >> 8) / 16777216.0) * 2.0 - 1.0;
    if (type == WAVE_NOISE_WHITE) return white;
    if (type == WAVE_NOISE_PINK) {
        // Paul Kellet 递归 pink 滤波
        pink_b0 = 0.99886 * pink_b0 + white * 0.0555179;
        pink_b1 = 0.99332 * pink_b1 + white * 0.0750759;
        pink_b2 = 0.96900 * pink_b2 + white * 0.1538520;
        pink_b3 = 0.86650 * pink_b3 + white * 0.3104856;
        pink_b4 = 0.55000 * pink_b4 + white * 0.5329522;
        pink_b5 = -0.7616 * pink_b5 - white * 0.0168980;
        double out = pink_b0 + pink_b1 + pink_b2 + pink_b3 +
                     pink_b4 + pink_b5 + pink_b6 + white * 0.5362;
        pink_b6 = white * 0.115926;
        return out * 0.11;                     // 归一化
    }
    // brown
    brown_last += 0.02 * white;
    if (brown_last > 1.0) brown_last = 1.0;
    if (brown_last < -1.0) brown_last = -1.0;
    return brown_last * 3.0;
}

void NoiseGenerator::process_block(double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process();
}

// ============================================================================
// 自检
// ============================================================================
int osc_self_test() {
    int fail = 0;
    sine_table_init();

    // 1. 正弦表对称性：sin(pi)=0, sin(pi/2)=1
    double s_half = sine_lookup(0.25);         // 1/4 周期 = pi/2
    double s_mid  = sine_lookup(0.50);         // 1/2 周期 = pi

    // 2. 正弦振荡器一个完整周期应回到近似 0
    Oscillator osc;
    osc.init();
    osc.set_waveform(WAVE_SINE);
    osc.set_frequency(1.0);                   // 1 Hz
    set_sample_rate(100.0);                    // 100 采样/秒 -> 100 采样/周期
    double v = 0.0;
    for (int i = 0; i < 100; i++) v = osc.process();

    // 3. 方波占空 0.5：50% 采样为 +1, 50% 为 -1
    osc.init();
    osc.set_waveform(WAVE_SQUARE);
    osc.set_frequency(100.0);                  // 100 Hz @ 100Hz 采样 = 1 采样/半周期
    int pos = 0, neg = 0;
    for (int i = 0; i < 1000; i++) {
        double x = osc.process();
        if (x > 0.5) pos++;
        else if (x < -0.5) neg++;
    }

    // 4. 三角波峰值应在 ±1 附近
    osc.init();
    osc.set_waveform(WAVE_TRIANGLE);
    osc.set_frequency(1000.0);
    double peak = 0.0;
    for (int i = 0; i < 2000; i++) {
        double x = osc.process();
        if (fabs(x) > peak) peak = fabs(x);
    }

    // 5. FM 算子：无调制时输出正弦，峰值在 [-1,1]
    FmOperator fm;
    fm.init();
    fm.set_frequency(440.0);
    fm.set_mod_depth(0.0);
    double fm_peak = 0.0;
    for (int i = 0; i < 1000; i++) {
        double x = fm.process(0.0);
        if (fabs(x) > fm_peak) fm_peak = fabs(x);
    }

    // 6. 白噪声均值应接近 0（长序列）
    NoiseGenerator ng;
    ng.init(WAVE_NOISE_WHITE);
    double sum = 0.0;
    for (int i = 0; i < 10000; i++) sum += ng.process();
    double mean = sum / 10000.0;

    // 7. 波表振荡器：加载三角表，输出范围正确
    double tbl[8] = { -1.0, -0.5, 0.0, 0.5, 1.0, 0.5, 0.0, -0.5 };
    WavetableOsc wto;
    wto.load_table(tbl, 8);
    wto.set_frequency(100.0);
    double wpeak = 0.0;
    for (int i = 0; i < 1000; i++) {
        double x = wto.process();
        if (fabs(x) > wpeak) wpeak = fabs(x);
    }

    set_sample_rate(44100.0);                  // 恢复默认
    return fail;
}

} // namespace audsp
} // namespace nefu



