// ============================================================================
// nefuOS 音频DSP库 —— 信号分析模块 (analysis.h)
// ----------------------------------------------------------------------------
// 提供：
//   * FFT 基2 时间抽取 (DIT)，double 实/虚数组
//   * IFFT
//   * DCT-II / DCT-III / DCT-IV
//   * STFT (短时傅里叶变换)
//   * 窗函数：Hann / Hamming / Blackman / Kaiser / Nuttall
//   * 自相关
//   * 过零率 + 基频估计
//   * RMS / 峰值
// ============================================================================
#pragma once
#include <stdint.h>

namespace nefu {
namespace audsp {

// ---- 复数对 ----
struct Complex { double re, im; };

// ============================================================================
// FFT
// ----------------------------------------------------------------------------
// n 必须是 2 的幂。原地做位反转 + Butterfly。
// data[2*i] = re, data[2*i+1] = im。
// ============================================================================
bool fft_is_power_of_two(int n);
void fft_bit_reverse(double* data, int n);           // 原地重排
void fft_forward(double* data, int n);               // 正变换
void fft_inverse(double* data, int n);               // 逆变换（含 1/n 归一化）
double fft_mag(const double* data, int bin);        // 取某 bin 幅度

// ============================================================================
// DCT
// ============================================================================
void dct_ii(const double* in, double* out, int n);   // 长度 n
void dct_iii(const double* in, double* out, int n);
void dct_iv(const double* in, double* out, int n);

// ============================================================================
// 窗函数
// ============================================================================
enum WindowType {
    WIN_RECT = 0,
    WIN_HANN,
    WIN_HAMMING,
    WIN_BLACKMAN,
    WIN_KAISER,
    WIN_NUTTALL
};
void window_apply(double* data, int n, int type);     // 原地乘窗
double window_sum(const double* data, int n);         // 窗积分（用于归一化）

// ============================================================================
// STFT
// ----------------------------------------------------------------------------
// 对输入 x[nsamples] 做 STFT：
//   frame 长 fft_size，hop = hop_samples，每帧加窗后 FFT。
// 输出 magnitude 矩阵：frames x (fft_size/2+1)，由调用者分配。
// ============================================================================
struct StftConfig {
    int fft_size;
    int hop;
    int window;
};
int  stft_analyze(const double* x, int nsamples,
                  const StftConfig& cfg,
                  double* mag_out,                  // [n_frames * (fft_size/2+1)]
                  int max_frames);

// ============================================================================
// 自相关
// ============================================================================
void autocorrelate(const double* x, int n, double* out, int max_lag);

// ============================================================================
// 基频估计
// ============================================================================
double estimate_pitch_autocorr(const double* x, int n, double sample_rate);
double estimate_zcr(const double* x, int n);          // 过零率 (每秒)

// ============================================================================
// 电平
// ============================================================================
double rms_level(const double* x, int n);
double peak_level(const double* x, int n);

// ============================================================================
// 自检
// ============================================================================
int analysis_self_test();

} // namespace audsp
} // namespace nefu
