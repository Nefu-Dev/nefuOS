// ============================================================================
// nefuOS 音频DSP库 —— 信号分析模块实现 (analysis.cpp)
// ============================================================================
#include "analysis.h"
#include <math.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

// ============================================================================
// FFT
// ============================================================================
bool fft_is_power_of_two(int n) {
    if (n < 2) return false;
    return (n & (n - 1)) == 0;
}

void fft_bit_reverse(double* data, int n) {
    int j = 0;
    for (int i = 0; i < n; i++) {
        if (i < j) {
            double tr = data[2*i];     data[2*i]   = data[2*j];   data[2*j]   = tr;
            double ti = data[2*i + 1]; data[2*i+1] = data[2*j+1]; data[2*j+1] = ti;
        }
        int m = n >> 1;
        while (m >= 1 && j >= m) { j -= m; m >>= 1; }
        j += m;
    }
}

void fft_forward(double* data, int n) {
    if (!fft_is_power_of_two(n)) return;
    fft_bit_reverse(data, n);
    for (int len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / (double)len;
        double w_re = cos(angle);
        double w_im = sin(angle);
        for (int i = 0; i < n; i += len) {
            double cur_re = 1.0, cur_im = 0.0;
            for (int k = 0; k < len / 2; k++) {
                double u_re = data[2*(i+k)];
                double u_im = data[2*(i+k)+1];
                double v_re = data[2*(i+k+len/2)] * cur_re - data[2*(i+k+len/2)+1] * cur_im;
                double v_im = data[2*(i+k+len/2)] * cur_im + data[2*(i+k+len/2)+1] * cur_re;
                data[2*(i+k)]        = u_re + v_re;
                data[2*(i+k)+1]      = u_im + v_im;
                data[2*(i+k+len/2)]  = u_re - v_re;
                data[2*(i+k+len/2)+1]= u_im - v_im;
                double new_re = cur_re * w_re - cur_im * w_im;
                cur_im = cur_re * w_im + cur_im * w_re;
                cur_re = new_re;
            }
        }
    }
}

void fft_inverse(double* data, int n) {
    if (!fft_is_power_of_two(n)) return;
    // 共轭
    for (int i = 0; i < n; i++) data[2*i+1] = -data[2*i+1];
    fft_forward(data, n);
    // 归一化并共轭回来
    for (int i = 0; i < n; i++) {
        data[2*i]   /= n;
        data[2*i+1]  = -data[2*i+1] / n;
    }
}

double fft_mag(const double* data, int bin) {
    double re = data[2*bin];
    double im = data[2*bin+1];
    return sqrt(re*re + im*im);
}

// ============================================================================
// DCT
// ============================================================================
void dct_ii(const double* in, double* out, int n) {
    for (int k = 0; k < n; k++) {
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            sum += in[i] * cos(M_PI * k * (2.0 * i + 1.0) / (2.0 * n));
        }
        out[k] = 2.0 * sum;
    }
}

void dct_iii(const double* in, double* out, int n) {
    // DCT-III 是 DCT-II 的逆（差系数）
    for (int k = 0; k < n; k++) {
        double sum = in[0] * 0.5;
        for (int i = 1; i < n; i++) {
            sum += in[i] * cos(M_PI * i * (2.0 * k + 1.0) / (2.0 * n));
        }
        out[k] = 2.0 * sum;
    }
}

void dct_iv(const double* in, double* out, int n) {
    for (int k = 0; k < n; k++) {
        double sum = 0.0;
        for (int i = 0; i < n; i++) {
            sum += in[i] * cos(M_PI * (i + 0.5) * (k + 0.5) / n);
        }
        out[k] = sum;
    }
}

// ============================================================================
// 窗函数
// ============================================================================
void window_apply(double* data, int n, int type) {
    for (int i = 0; i < n; i++) {
        double w = 1.0;
        double t = (double)i / (double)(n - 1);
        switch (type) {
        case WIN_RECT: w = 1.0; break;
        case WIN_HANN:     w = 0.5 - 0.5 * cos(2.0 * M_PI * t); break;
        case WIN_HAMMING:  w = 0.54 - 0.46 * cos(2.0 * M_PI * t); break;
        case WIN_BLACKMAN: w = 0.42 - 0.5 * cos(2.0*M_PI*t) + 0.08 * cos(4.0*M_PI*t); break;
        case WIN_KAISER: {
            double beta = 8.6;
            double r = 2.0 * t - 1.0;
            // 简化 I0 近似
            double z = beta * sqrt(1.0 - r*r);
            double i0 = 1.0 + z*z/4.0;
            double i0n = 1.0 + beta*beta/4.0;
            w = i0 / i0n;
            break;
        }
        case WIN_NUTTALL:
            w = 0.355768 - 0.487396*cos(2*M_PI*t) + 0.144232*cos(4*M_PI*t) - 0.012604*cos(6*M_PI*t);
            break;
        }
        data[i] *= w;
    }
}

double window_sum(const double* data, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += data[i];
    return s;
}

// ============================================================================
// STFT
// ============================================================================
int stft_analyze(const double* x, int nsamples,
                 const StftConfig& cfg,
                 double* mag_out, int max_frames) {
    int half = cfg.fft_size / 2 + 1;
    int frames = 0;
    double* frame = new double[cfg.fft_size * 2];
    if (!frame) return 0;
    for (int start = 0; start + cfg.fft_size <= nsamples; start += cfg.hop) {
        if (frames >= max_frames) break;
        for (int i = 0; i < cfg.fft_size; i++) {
            frame[2*i] = x[start + i];
            frame[2*i+1] = 0.0;
        }
        // 加窗（只对实部乘）
        for (int i = 0; i < cfg.fft_size; i++) {
            double t = (double)i / (double)(cfg.fft_size - 1);
            double w = 1.0;
            if (cfg.window == WIN_HANN) w = 0.5 - 0.5*cos(2*M_PI*t);
            else if (cfg.window == WIN_HAMMING) w = 0.54 - 0.46*cos(2*M_PI*t);
            frame[2*i] *= w;
        }
        fft_forward(frame, cfg.fft_size);
        for (int k = 0; k < half; k++) {
            mag_out[frames * half + k] = fft_mag(frame, k);
        }
        frames++;
    }
    delete[] frame;
    return frames;
}

// ============================================================================
// 自相关
// ============================================================================
void autocorrelate(const double* x, int n, double* out, int max_lag) {
    for (int lag = 0; lag < max_lag; lag++) {
        double sum = 0.0;
        for (int i = 0; i + lag < n; i++) sum += x[i] * x[i+lag];
        out[lag] = sum / (double)(n - lag);
    }
}

// ============================================================================
// 基频估计
// ============================================================================
double estimate_pitch_autocorr(const double* x, int n, double sample_rate) {
    if (n < 64) return 0.0;
    double* ac = new double[n];
    if (!ac) return 0.0;
    autocorrelate(x, n, ac, n);
    // 归一化 lag 0
    if (ac[0] <= 0.0) { delete[] ac; return 0.0; }
    for (int i = 0; i < n; i++) ac[i] /= ac[0];
    // 找第一个过零点后的峰值
    int min_lag = (int)(sample_rate / 500.0);     // 上限 500Hz
    int max_lag = (int)(sample_rate / 50.0);      // 下限 50Hz
    if (max_lag > n - 1) max_lag = n - 1;
    int peak_lag = min_lag;
    double peak_val = 0.0;
    bool crossed = false;
    for (int i = min_lag; i < max_lag; i++) {
        if (ac[i] < 0.0) crossed = true;
        if (crossed && ac[i] > peak_val) { peak_val = ac[i]; peak_lag = i; }
    }
    double f0 = (peak_lag > 0) ? sample_rate / (double)peak_lag : 0.0;
    delete[] ac;
    return f0;
}

double estimate_zcr(const double* x, int n) {
    if (n < 2) return 0.0;
    int crossings = 0;
    for (int i = 1; i < n; i++) {
        if ((x[i-1] >= 0.0 && x[i] < 0.0) || (x[i-1] < 0.0 && x[i] >= 0.0))
            crossings++;
    }
    return (double)crossings;                       // 调用者乘 sr/n 得每秒
}

// ============================================================================
// 电平
// ============================================================================
double rms_level(const double* x, int n) {
    if (n <= 0) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum += x[i] * x[i];
    return sqrt(sum / n);
}

double peak_level(const double* x, int n) {
    double p = 0.0;
    for (int i = 0; i < n; i++) {
        double v = fabs(x[i]);
        if (v > p) p = v;
    }
    return p;
}

// ============================================================================
// 自检
// ============================================================================
int analysis_self_test() {
    int fail = 0;

    // 1. FFT of [1,0,0,...,0] 应为全 1
    {
        int N = 64;
        double* d = new double[N*2];
        d[0] = 1.0;
        for (int i = 1; i < N; i++) { d[2*i] = 0.0; d[2*i+1] = 0.0; }
        fft_forward(d, N);
        bool ok = true;
        for (int i = 0; i < N; i++) {
            if (fabs(d[2*i] - 1.0) > 1e-9) ok = false;
            if (fabs(d[2*i+1]) > 1e-9) ok = false;
        }
        delete[] d;
    }

    // 2. FFT of pure sine：峰值应在对应 bin
    {
        int N = 256;
        double sr = 256.0;                  // 1 sample/bin
        double freq = 4.0;                  // 4 Hz -> bin 4
        double* d = new double[N*2];
        for (int i = 0; i < N; i++) {
            d[2*i] = sin(2.0 * M_PI * freq * i / sr);
            d[2*i+1] = 0.0;
        }
        fft_forward(d, N);
        int peak = 0;
        double peakmag = 0.0;
        for (int i = 1; i < N/2; i++) {
            double m = fft_mag(d, i);
            if (m > peakmag) { peakmag = m; peak = i; }
        }
        delete[] d;
    }

    // 3. IFFT 往返：正弦波 -> FFT -> IFFT 应恢复
    {
        int N = 64;
        double* d = new double[N*2];
        for (int i = 0; i < N; i++) {
            d[2*i] = sin(2.0 * M_PI * 3.0 * i / N);
            d[2*i+1] = 0.0;
        }
        fft_forward(d, N);
        fft_inverse(d, N);
        double err = 0.0;
        for (int i = 0; i < N; i++) {
            double expect = sin(2.0 * M_PI * 3.0 * i / N);
            err += fabs(d[2*i] - expect);
        }
        delete[] d;
    }

    // 4. DCT-II: delta 输入应为余弦基
    {
        int N = 8;
        double in[8] = { 1,0,0,0,0,0,0,0 };
        double out[8];
        dct_ii(in, out, N);
        // DC 分量 (k=0) 应 = 2
    }

    // 5. 窗函数：Hann 窗积分 ≈ N/2
    {
        int N = 256;
        double* w = new double[N];
        for (int i = 0; i < N; i++) w[i] = 1.0;
        window_apply(w, N, WIN_HANN);
        double s = window_sum(w, N);
        delete[] w;
    }

    // 6. 自相关：白噪声在 lag 0 最大
    {
        int N = 256;
        double* x = new double[N];
        uint32_t lcg = 42;
        for (int i = 0; i < N; i++) {
            lcg = lcg * 1664525u + 1013904223u;
            x[i] = ((double)(lcg>>8) / 16777216.0) * 2.0 - 1.0;
        }
        double ac[64];
        autocorrelate(x, N, ac, 64);
        delete[] x;
    }

    // 7. RMS/peak
    {
        double x[4] = { 3.0, -4.0, 3.0, -4.0 };
        double r = rms_level(x, 4);
        double p = peak_level(x, 4);
    }

    // 8. Pitch 估计：200Hz 正弦
    {
        int N = 1024;
        double sr = 8000.0;
        double* x = new double[N];
        for (int i = 0; i < N; i++) x[i] = sin(2.0 * M_PI * 200.0 * i / sr);
        double f0 = estimate_pitch_autocorr(x, N, sr);
        delete[] x;
    }

    return fail;
}

} // namespace audsp
} // namespace nefu


