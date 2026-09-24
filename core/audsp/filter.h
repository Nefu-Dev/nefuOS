// ============================================================================
// nefuOS 音频DSP库 —— 滤波器模块 (filter.h)
// ----------------------------------------------------------------------------
// 提供：
//   * Biquad 二阶节 (RBJ  cookbook 系数)：
//       LOWPASS / HIGHPASS / BANDPASS / NOTCH / PEAKING / LOWSHELF / HIGHSHELF
//   * OnePole 一阶低/高通
//   * StateVariable Filter (同时输出 LP/BP/HP)
//   * Comb (前馈/反馈)，用于混响
//   * Allpass 全通
//   * FIR windowed-sinc (低通)
//   * Moog ladder 简化版 (4 级饱和低通)
//
// 所有滤波器归一化到全局采样率 g_sample_rate。
// ============================================================================
#pragma once
#include <stdint.h>

namespace nefu {
namespace audsp {

// ---- Biquad 类型 ----
enum BiquadType {
    BIQUAD_LOWPASS = 0,
    BIQUAD_HIGHPASS,
    BIQUAD_BANDPASS,
    BIQUAD_NOTCH,
    BIQUAD_PEAKING,
    BIQUAD_LOWSHELF,
    BIQUAD_HIGHSHELF
};

const char* biquad_type_name(int t);

// ============================================================================
// Biquad —— 标准双二阶 IIR
// ----------------------------------------------------------------------------
// 差分方程：
//   y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
// a0 归一化到 1。
// ============================================================================
struct Biquad {
    double b0, b1, b2;
    double a1, a2;
    double x1, x2;       // 输入历史
    double y1, y2;       // 输出历史

    void init();
    // 计算 RBJ 系数：
    //   type: BiquadType
    //   freq: 截止/中心频率 Hz
    //   q:    Q 值 (shelf 时用作 S 斜率)
    //   gain_db: 仅 peaking/shelf 有用
    void compute(int type, double freq, double q, double gain_db = 0.0);
    double process(double x);
    void process_block(const double* in, double* out, int n);
};

// ============================================================================
// OnePole —— 一阶低通 (y += cutoff*(x-y))
// ============================================================================
struct OnePole {
    double coeff;        // 0..1，越大越接近全通
    double state;
    void init(double cutoff_hz);
    double process(double x);
};

// ============================================================================
// StateVariable Filter
// ----------------------------------------------------------------------------
// 同时输出低通/带通/高通：
//   low  += f * band
//   high = in - low - q * band
//   band = high * f + band
// ============================================================================
struct StateVariable {
    double f;            // 频率系数
    double q;            // 共振
    double low, band, high;
    void init(double cutoff_hz, double resonance);
    void process(double in, double& out_lp, double& out_bp, double& out_hp);
};

// ============================================================================
// CombFilter —— 梳状滤波
// ----------------------------------------------------------------------------
// 前馈: y = x + fb * delayed
// 反馈: y = x + fb * y_delayed
// 延时以采样为单位，内部环形缓冲。
// ============================================================================
struct CombFilter {
    double* buffer;
    int     size;
    int     idx;
    double  feedback;    // 0..~0.95
    bool    feedforward; // true=前馈, false=反馈
    double  last;

    CombFilter();
    ~CombFilter();
    bool init(double delay_seconds, double feedback_gain, bool ff = false);
    double process(double x);
    void process_block(const double* in, double* out, int n);
};

// ============================================================================
// AllpassFilter —— 全通
// ----------------------------------------------------------------------------
// y[n] = -g*x[n] + x[n-D] + g*y[n-D]
// 相位移动但幅度平坦，Schroeder 混响中使用。
// ============================================================================
struct AllpassFilter {
    double* buffer;
    int     size;
    int     idx;
    double  g;
    AllpassFilter();
    ~AllpassFilter();
    bool init(double delay_seconds, double g);
    double process(double x);
};

// ============================================================================
// FirFilter —— 加窗 sinc FIR 低通
// ----------------------------------------------------------------------------
// 长度 taps 个抽头；抽头系数由 sinc(2*fc*n) * window(n) 预计算。
// ============================================================================
struct FirFilter {
    double* coeffs;
    double* history;
    int     n_taps;
    int     pos;

    FirFilter();
    ~FirFilter();
    // cutoff_hz 相对采样率的归一化频率；window_type: 0=hann,1=hamming,2=blackman
    bool init(int taps, double cutoff_hz, int window_type = 0);
    double process(double x);
    void process_block(const double* in, double* out, int n);
};

// ============================================================================
// MoogLadder —— 简化 Moog 梯形低通
// ----------------------------------------------------------------------------
// 4 级一阶低通 + 软饱和；cutoff 0..1, resonance 0..1。
// ============================================================================
struct MoogLadder {
    double stage[4];
    double cutoff;       // 0..1 归一化
    double resonance;    // 0..1
    void init();
    void set_cutoff(double c);    // 0..1
    void set_resonance(double r); // 0..1
    double process(double x);
    void process_block(const double* in, double* out, int n);
};

// ============================================================================
// 自检
// ============================================================================
int filter_self_test();

} // namespace audsp
} // namespace nefu
