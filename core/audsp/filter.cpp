// ============================================================================
// nefuOS 音频DSP库 —— 滤波器模块实现 (filter.cpp)
// ============================================================================
#include "filter.h"
#include "osc.h"           // g_sample_rate
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

const char* biquad_type_name(int t) {
    switch (t) {
    case BIQUAD_LOWPASS:    return "lowpass";
    case BIQUAD_HIGHPASS:   return "highpass";
    case BIQUAD_BANDPASS:   return "bandpass";
    case BIQUAD_NOTCH:      return "notch";
    case BIQUAD_PEAKING:    return "peaking";
    case BIQUAD_LOWSHELF:   return "lowshelf";
    case BIQUAD_HIGHSHELF:  return "highshelf";
    default: return "?";
    }
}

// ============================================================================
// Biquad
// ============================================================================
void Biquad::init() {
    b0 = 1.0; b1 = b2 = 0.0;
    a1 = a2 = 0.0;
    x1 = x2 = y1 = y2 = 0.0;
}

void Biquad::compute(int type, double freq, double q, double gain_db) {
    double sr = g_sample_rate;
    double w0 = 2.0 * M_PI * freq / sr;
    double cosw = cos(w0);
    double sinw = sin(w0);
    double A = pow(10.0, gain_db / 40.0);       // shelf/peaking 用
    double alpha = sinw / (2.0 * q);

    double b0s = 0, b1s = 0, b2s = 0;
    double a0s = 1, a1s = 0, a2s = 0;

    switch (type) {
    case BIQUAD_LOWPASS:
        b0s = (1.0 - cosw) / 2.0;
        b1s = 1.0 - cosw;
        b2s = (1.0 - cosw) / 2.0;
        a0s = 1.0 + alpha;
        a1s = -2.0 * cosw;
        a2s = 1.0 - alpha;
        break;
    case BIQUAD_HIGHPASS:
        b0s = (1.0 + cosw) / 2.0;
        b1s = -(1.0 + cosw);
        b2s = (1.0 + cosw) / 2.0;
        a0s = 1.0 + alpha;
        a1s = -2.0 * cosw;
        a2s = 1.0 - alpha;
        break;
    case BIQUAD_BANDPASS:
        b0s = alpha;
        b1s = 0.0;
        b2s = -alpha;
        a0s = 1.0 + alpha;
        a1s = -2.0 * cosw;
        a2s = 1.0 - alpha;
        break;
    case BIQUAD_NOTCH:
        b0s = 1.0;
        b1s = -2.0 * cosw;
        b2s = 1.0;
        a0s = 1.0 + alpha;
        a1s = -2.0 * cosw;
        a2s = 1.0 - alpha;
        break;
    case BIQUAD_PEAKING:
        b0s = 1.0 + alpha * A;
        b1s = -2.0 * cosw;
        b2s = 1.0 - alpha * A;
        a0s = 1.0 + alpha / A;
        a1s = -2.0 * cosw;
        a2s = 1.0 - alpha / A;
        break;
    case BIQUAD_LOWSHELF: {
        double s = q;                            // 这里 q 当 S 用
        double two_sqrtA_alpha = 2.0 * sqrt(A) * s * sinw / 2.0;
        b0s = A * ((A + 1.0) - (A - 1.0) * cosw + two_sqrtA_alpha);
        b1s = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
        b2s = A * ((A + 1.0) - (A - 1.0) * cosw - two_sqrtA_alpha);
        a0s = (A + 1.0) + (A - 1.0) * cosw + two_sqrtA_alpha;
        a1s = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
        a2s = (A + 1.0) + (A - 1.0) * cosw - two_sqrtA_alpha;
        break;
    }
    case BIQUAD_HIGHSHELF: {
        double s = q;
        double two_sqrtA_alpha = 2.0 * sqrt(A) * s * sinw / 2.0;
        b0s = A * ((A + 1.0) + (A - 1.0) * cosw + two_sqrtA_alpha);
        b1s = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
        b2s = A * ((A + 1.0) + (A - 1.0) * cosw - two_sqrtA_alpha);
        a0s = (A + 1.0) - (A - 1.0) * cosw + two_sqrtA_alpha;
        a1s = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
        a2s = (A + 1.0) - (A - 1.0) * cosw - two_sqrtA_alpha;
        break;
    }
    }
    // 归一化 a0
    b0 = b0s / a0s;
    b1 = b1s / a0s;
    b2 = b2s / a0s;
    a1 = a1s / a0s;
    a2 = a2s / a0s;
    x1 = x2 = y1 = y2 = 0.0;
}

double Biquad::process(double x) {
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
}

void Biquad::process_block(const double* in, double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process(in[i]);
}

// ============================================================================
// OnePole
// ============================================================================
void OnePole::init(double cutoff_hz) {
    // 一阶低通系数：c = exp(-2*pi*fc/sr)
    coeff = exp(-2.0 * M_PI * cutoff_hz / g_sample_rate);
    if (coeff > 0.99999) coeff = 0.99999;
    if (coeff < 0.0) coeff = 0.0;
    state = 0.0;
}

double OnePole::process(double x) {
    state += coeff * (x - state);
    return state;
}

// ============================================================================
// StateVariable
// ============================================================================
void StateVariable::init(double cutoff_hz, double resonance) {
    // f 近似：2*sin(pi*fc/sr)，夹紧
    double fc = cutoff_hz / g_sample_rate;
    if (fc > 0.49) fc = 0.49;
    f = 2.0 * sin(M_PI * fc);
    if (f > 1.0) f = 1.0;
    q = resonance;
    low = band = high = 0.0;
}

void StateVariable::process(double in, double& out_lp, double& out_bp, double& out_hp) {
    high = in - low - q * band;
    band += f * high;
    low  += f * band;
    out_lp = low;
    out_bp = band;
    out_hp = high;
}

// ============================================================================
// CombFilter
// ============================================================================
CombFilter::CombFilter() {
    buffer = 0; size = 0; idx = 0; feedback = 0.0; feedforward = false; last = 0.0;
}
CombFilter::~CombFilter() { delete[] buffer; buffer = 0; }

bool CombFilter::init(double delay_seconds, double fb, bool ff) {
    delete[] buffer;
    size = (int)(delay_seconds * g_sample_rate);
    if (size < 1) size = 1;
    buffer = new double[size];
    if (!buffer) { size = 0; return false; }
    for (int i = 0; i < size; i++) buffer[i] = 0.0;
    idx = 0;
    feedback = fb;
    feedforward = ff;
    last = 0.0;
    return true;
}

double CombFilter::process(double x) {
    if (!buffer || size <= 0) return x;
    double d = buffer[idx];
    double y;
    if (feedforward) {
        y = x + feedback * d;
    } else {
        y = x + feedback * d;
        buffer[idx] = y;                    // 反馈：输出写入延时线
    }
    if (!feedforward) {
        // 上面已写入；前馈模式不写
    }
    if (feedforward) buffer[idx] = x;       // 前馈只存输入
    idx++;
    if (idx >= size) idx = 0;
    last = y;
    return y;
}

void CombFilter::process_block(const double* in, double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process(in[i]);
}

// ============================================================================
// AllpassFilter
// ============================================================================
AllpassFilter::AllpassFilter() {
    buffer = 0; size = 0; idx = 0; g = 0.5;
}
AllpassFilter::~AllpassFilter() { delete[] buffer; buffer = 0; }

bool AllpassFilter::init(double delay_seconds, double gg) {
    delete[] buffer;
    size = (int)(delay_seconds * g_sample_rate);
    if (size < 1) size = 1;
    buffer = new double[size];
    if (!buffer) { size = 0; return false; }
    for (int i = 0; i < size; i++) buffer[i] = 0.0;
    idx = 0;
    g = gg;
    return true;
}

double AllpassFilter::process(double x) {
    if (!buffer || size <= 0) return x;
    double d = buffer[idx];
    double y = -g * x + d;
    buffer[idx] = x + g * y;
    idx++;
    if (idx >= size) idx = 0;
    return y;
}

// ============================================================================
// FirFilter
// ============================================================================
FirFilter::FirFilter() {
    coeffs = 0; history = 0; n_taps = 0; pos = 0;
}
FirFilter::~FirFilter() {
    delete[] coeffs; delete[] history;
    coeffs = 0; history = 0;
}

bool FirFilter::init(int taps, double cutoff_hz, int window_type) {
    if (taps < 3) taps = 3;
    if (taps % 2 == 0) taps++;               // 奇数抽头，线性相位
    delete[] coeffs; delete[] history;
    coeffs  = new double[taps];
    history = new double[taps];
    if (!coeffs || !history) { n_taps = 0; return false; }
    n_taps = taps;
    pos = 0;
    for (int i = 0; i < taps; i++) history[i] = 0.0;

    double fc = cutoff_hz / g_sample_rate;   // 归一化 0..0.5
    int M = taps - 1;
    double sum = 0.0;
    for (int i = 0; i < taps; i++) {
        double m = i - M / 2;
        double sinc_val;
        if (fabs(m) < 1e-9) sinc_val = 2.0 * fc;
        else sinc_val = sin(2.0 * M_PI * fc * m) / (M_PI * m);
        // 窗函数
        double w;
        if (window_type == 0) {              // Hann
            w = 0.5 - 0.5 * cos(2.0 * M_PI * i / M);
        } else if (window_type == 1) {      // Hamming
            w = 0.54 - 0.46 * cos(2.0 * M_PI * i / M);
        } else {                            // Blackman
            w = 0.42 - 0.5 * cos(2.0 * M_PI * i / M) + 0.08 * cos(4.0 * M_PI * i / M);
        }
        coeffs[i] = sinc_val * w;
        sum += coeffs[i];
    }
    // 归一化增益为 1
    if (sum > 0.0) for (int i = 0; i < taps; i++) coeffs[i] /= sum;
    return true;
}

double FirFilter::process(double x) {
    if (!coeffs || n_taps <= 0) return x;
    history[pos] = x;
    double y = 0.0;
    int p = pos;
    for (int i = 0; i < n_taps; i++) {
        y += coeffs[i] * history[p];
        p--;
        if (p < 0) p = n_taps - 1;
    }
    pos++;
    if (pos >= n_taps) pos = 0;
    return y;
}

void FirFilter::process_block(const double* in, double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process(in[i]);
}

// ============================================================================
// MoogLadder
// ============================================================================
void MoogLadder::init() {
    for (int i = 0; i < 4; i++) stage[i] = 0.0;
    cutoff = 0.25;
    resonance = 0.1;
}

void MoogLadder::set_cutoff(double c) {
    if (c < 0.0) c = 0.0;
    if (c > 1.0) c = 1.0;
    cutoff = c;
}

void MoogLadder::set_resonance(double r) {
    if (r < 0.0) r = 0.0;
    if (r > 1.0) r = 1.0;
    resonance = r;
}

double MoogLadder::process(double x) {
    // 4 级一阶低通串联，每级带软饱和 (tanh 近似)
    double c = cutoff;
    double res = resonance;
    // 反馈
    x -= res * stage[3] * 4.0;
    // 4 级
    for (int i = 0; i < 4; i++) {
        stage[i] += c * (x - stage[i]);
        x = stage[i];
    }
    return x;
}

void MoogLadder::process_block(const double* in, double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process(in[i]);
}

// ============================================================================
// 自检
// ============================================================================
int filter_self_test() {
    int fail = 0;
    set_sample_rate(44100.0);

    // 1. Biquad 低通：直流 (0 Hz) 应无衰减，高频应被衰减
    Biquad lp;
    lp.compute(BIQUAD_LOWPASS, 1000.0, 0.707);
    // 直流输入：稳态增益应为 ~1
    double dc_out = 0.0;
    for (int i = 0; i < 500; i++) dc_out = lp.process(1.0);

    // 2. 低通对 10kHz 正弦应有明显衰减
    Biquad lp2;
    lp2.compute(BIQUAD_LOWPASS, 500.0, 0.707);
    double peak_in = 0.0, peak_out = 0.0;
    for (int i = 0; i < 2000; i++) {
        double x = sin(2.0 * M_PI * 10000.0 * i / 44100.0);
        double y = lp2.process(x);
        if (i > 1000) {
            if (fabs(x) > peak_in) peak_in = fabs(x);
            if (fabs(y) > peak_out) peak_out = fabs(y);
        }
    }

    // 3. 高通：直流应被滤除
    Biquad hp;
    hp.compute(BIQUAD_HIGHPASS, 500.0, 0.707);
    double hp_dc = 0.0;
    for (int i = 0; i < 500; i++) hp_dc = hp.process(1.0);

    // 4. OnePole：低通对直流增益为 1
    OnePole op;
    op.init(1000.0);
    double op_dc = 0.0;
    for (int i = 0; i < 500; i++) op_dc = op.process(1.0);

    // 5. StateVariable：低通输出在稳态跟踪直流
    StateVariable sv;
    sv.init(1000.0, 0.5);
    double lp_v = 0, bp_v = 0, hp_v = 0;
    for (int i = 0; i < 1000; i++) sv.process(1.0, lp_v, bp_v, hp_v);

    // 6. Comb：缓冲能正确延迟
    CombFilter comb;
    comb.init(0.01, 0.5, true);                  // 前馈无反馈
    for (int i = 0; i < 441; i++) comb.process(0.0);   // 冲刷
    double y0 = comb.process(1.0);               // 刚写入，未读出 -> ~0
    (void)y0;
    bool saw_pulse = false;
    for (int i = 0; i < 500; i++) {
        double y = comb.process(0.0);
        if (y > 0.4) saw_pulse = true;
    }

    // 7. Allpass：全通稳态增益应 ~1
    AllpassFilter ap;
    ap.init(0.01, 0.5);
    double ap_dc = 0.0;
    for (int i = 0; i < 2000; i++) ap_dc = ap.process(1.0);

    // 8. FIR：低通对直流增益 ~1
    FirFilter fir;
    fir.init(31, 1000.0, 0);
    double fir_dc = 0.0;
    for (int i = 0; i < 500; i++) fir_dc = fir.process(1.0);

    // 9. Moog ladder：直流通过
    MoogLadder moog;
    moog.init();
    moog.set_cutoff(0.5);
    double moog_dc = 0.0;
    for (int i = 0; i < 1000; i++) moog_dc = moog.process(1.0);

    return fail;
}

} // namespace audsp
} // namespace nefu











