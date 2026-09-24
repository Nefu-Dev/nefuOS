// nefuOS audlib —— 滤波器 filter
// 教学版：一阶低通/高通与双二阶（biquad）带通滤波器，数字音频滤波。
// 用于音色塑形、去噪、效果器教学。class / STL / cmath / 中文注释。
#pragma once
#include <cmath>
#include "audlib/wave.h"

namespace nefu {
namespace audx {

// 双二阶滤波器（教学版：low-pass / high-pass / band-pass）
class BiquadFilter {
public:
    // 类型
    enum Type { LOWPASS = 0, HIGHPASS, BANDPASS };

    BiquadFilter();
    // 设置：类型、截止/中心频率、Q 值
    void setup(Type t, double freq, double q, int sample_rate);
    // 处理单个采样（状态保持）
    double process(double x);
    // 处理整个缓冲（就地）
    void process(AudioBuf& a);

    // 复位状态
    void reset();

    // ---- self test ----
    static int self_test();

private:
    double b0, b1, b2, a0, a1, a2;
    double x1, x2, y1, y2;
};

// 一阶低通（简单平滑）
class LowPass1 {
public:
    LowPass1() : alpha(0.2), y_prev(0) {}
    // 设置截止频率（采样率归一化）
    void set_cutoff(double fc, int sample_rate);
    double process(double x) { y_prev = y_prev + alpha * (x - y_prev); return y_prev; }
    // 处理缓冲
    void process(AudioBuf& a);
    void reset() { y_prev = 0; }

    static int self_test();

private:
    double alpha;
    double y_prev;
};

} // namespace audx
} // namespace nefu
