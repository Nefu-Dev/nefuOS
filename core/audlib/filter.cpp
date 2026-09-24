// nefuOS audlib —— 滤波器实现 + 自测
#include "audlib/filter.h"
#include <cstdio>

namespace nefu {
namespace audx {

BiquadFilter::BiquadFilter() : b0(1), b1(0), b2(0), a0(1), a1(0), a2(0),
                               x1(0), x2(0), y1(0), y2(0) {}

void BiquadFilter::setup(Type t, double freq, double q, int sample_rate) {
    double w = 2 * 3.14159265358979 * freq / sample_rate;
    double c = std::cos(w), s = std::sin(w);
    double alpha = s / (2 * q);
    switch (t) {
        case LOWPASS:
            b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2;
            a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            break;
        case HIGHPASS:
            b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
            a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            break;
        case BANDPASS:
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
            break;
    }
    // 归一化 a0
    b0 /= a0; b1 /= a0; b2 /= a0;
    a1 /= a0; a2 /= a0;
}

double BiquadFilter::process(double x) {
    double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x;
    y2 = y1; y1 = y;
    return y;
}

void BiquadFilter::process(AudioBuf& a) {
    for (int i = 0; i < a.frames(); i++) {
        for (int c = 0; c < a.channels; c++) {
            int idx = i * a.channels + c;
            a.data[idx] = (short)process(a.data[idx]);
        }
    }
}

void BiquadFilter::reset() { x1 = x2 = y1 = y2 = 0; }

// ---- self test ----
int BiquadFilter::self_test() {
    int fails = 0;
    // 1. 低频通过（直流不变）
    {
        BiquadFilter f;
        f.setup(BiquadFilter::LOWPASS, 1000, 0.7, 8000);
        double y = 0;
        for (int i = 0; i < 500; i++) y = f.process(0.5);
        if (std::abs(y - 0.5) > 1e-4) fails++;   // 稳态后 DC 增益应为 1
    }
    // 2. 高频衰减（快速交替采样应大幅衰减）
    {
        BiquadFilter f;
        f.setup(BiquadFilter::LOWPASS, 500, 0.7, 8000);
        double mx = 0;
        double v = 0;
        for (int i = 0; i < 2000; i++) {
            v = f.process((i % 2 == 0) ? 1.0 : -1.0);   // 4000Hz 方波
            if (v < 0) v = -v;
            if (v > mx) mx = v;
        }
        if (mx > 0.3) fails++;   // 高频应被显著衰减
    }
    // 3. 高通相反
    {
        BiquadFilter f;
        f.setup(BiquadFilter::HIGHPASS, 500, 0.7, 8000);
        // 直流应被滤除
        double y = 1;
        for (int i = 0; i < 100; i++) y = f.process(0.5);
        if (std::abs(y) > 1e-3) fails++;
    }
    // 4. 缓冲处理
    {
        AudioBuf a(8000, 1);
        a.silence(100);
        for (int i = 0; i < 100; i++) a.data[i] = 1000;
        BiquadFilter f;
        f.setup(BiquadFilter::LOWPASS, 2000, 0.7, 8000);
        f.process(a);
        if (a.data[0] == 0) fails++;   // 应有响应
    }
    // 5. 复位
    {
        BiquadFilter f;
        f.setup(BiquadFilter::LOWPASS, 1000, 1, 8000);
        f.process(1.0);
        f.reset();
        double y = f.process(1.0);   // 重新开始，非稳定状态
        if (y == 0) fails++;
    }
    return fails;
}

void LowPass1::set_cutoff(double fc, int sample_rate) {
    double dt = 1.0 / sample_rate;
    double rc = 1.0 / (2 * 3.14159265358979 * fc);
    alpha = dt / (rc + dt);
    if (alpha > 1) alpha = 1;
    if (alpha < 0) alpha = 0;
}

void LowPass1::process(AudioBuf& a) {
    for (int i = 0; i < a.frames(); i++) {
        for (int c = 0; c < a.channels; c++) {
            int idx = i * a.channels + c;
            y_prev = y_prev + alpha * (a.data[idx] - y_prev);
            a.data[idx] = (short)y_prev;
        }
    }
}

int LowPass1::self_test() {
    int fails = 0;
    LowPass1 f;
    f.set_cutoff(1000, 8000);
    // 1. 直流保持
    {
        double y = 0;
        for (int i = 0; i < 100; i++) y = f.process(0.4);
        if (std::abs(y - 0.4) > 1e-6) fails++;
    }
    // 2. 高频快速变化被平滑
    {
        LowPass1 g;
        g.set_cutoff(300, 8000);
        double mx = 0;
        double v = 0;
        for (int i = 0; i < 500; i++) {
            v = g.process((i % 2 == 0) ? 1.0 : -1.0);
            if (v < 0) v = -v;
            if (v > mx) mx = v;
        }
        if (mx > 0.6) fails++;
    }
    return fails;
}

} // namespace audx
} // namespace nefu
