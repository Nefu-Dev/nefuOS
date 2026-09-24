// nefuOS audlib —— 包络实现 + 自测
#include "audlib/env.h"
#include "audlib/wave.h"
#include "audlib/synth.h"
#include <cstdio>

namespace nefu {
namespace audx {

double Envelope::value(double t, double note_dur) const {
    if (t < 0) return 0;
    double tt = t;
    // 起音
    if (tt < a_time) {
        double g = a_time > 0 ? tt / a_time : 1;
        if (g > 1) g = 1;
        return g;
    }
    tt -= a_time;
    // 衰减
    if (tt < d_time) {
        double g = d_time > 0 ? 1.0 - (1.0 - s_level) * (tt / d_time) : s_level;
        if (g < 0) g = 0;
        return g;
    }
    tt -= d_time;
    // 保持段：剩余时间 = note_dur - (a+d)
    double hold = note_dur - a_time - d_time;
    if (hold < 0) hold = 0;
    if (tt < hold) return s_level;
    // 释音
    tt -= hold;
    double g = r_time > 0 ? s_level * (1.0 - tt / r_time) : 0;
    if (g < 0) g = 0;
    return g;
}

std::vector<double> Envelope::curve(int sample_rate, double note_dur) const {
    int n = (int)(sample_rate * note_dur);
    std::vector<double> c(n);
    for (int i = 0; i < n; i++) c[i] = value((double)i / sample_rate, note_dur);
    return c;
}

void Envelope::apply(AudioBuf& a, double note_dur) const {
    int n = a.frames();
    for (int i = 0; i < n; i++) {
        double g = value((double)i / a.sample_rate, note_dur);
        for (int c = 0; c < a.channels; c++)
            a.data[i * a.channels + c] = (short)(a.data[i * a.channels + c] * g);
    }
}

// ---- self test ----
int Envelope::self_test() {
    int fails = 0;
    Envelope e;
    e.a_time = 0.1; e.d_time = 0.1; e.s_level = 0.5; e.r_time = 0.2;
    // 1. 起音起点 0，终点 1
    {
        if (e.value(0, 1.0) > 1e-6) fails++;
        if (std::abs(e.value(0.1, 1.0) - 1.0) > 1e-6) fails++;
    }
    // 2. 衰减段线性降到 s_level
    {
        double mid = e.value(0.15, 1.0);   // 0.1..0.2 之间
        if (mid > 0.75 || mid < 0.25) fails++;
        double at_s = e.value(0.2, 1.0);
        if (std::abs(at_s - 0.5) > 1e-6) fails++;
    }
    // 3. 保持段恒定
    {
        double h1 = e.value(0.4, 1.0);
        double h2 = e.value(0.6, 1.0);
        if (std::abs(h1 - 0.5) > 1e-6 || std::abs(h2 - 0.5) > 1e-6) fails++;
    }
    // 4. 释音到 0
    {
        double e0 = e.value(1.0, 1.0);   // note_dur 结束：刚进入释音，仍为 s_level
        if (std::abs(e0 - 0.5) > 1e-6) fails++;
        double mid = e.value(1.1, 1.0);  // 释音中段（1.0..1.2）
        if (mid > 0.3 || mid < 0.05) fails++;
        double end = e.value(1.2, 1.0);  // 释音结束
        if (end > 1e-6) fails++;
    }
    // 5. 曲线长度
    {
        std::vector<double> c = e.curve(8000, 1.0);
        if ((int)c.size() != 8000) fails++;
        if (c[0] > 1e-6) fails++;
        if (std::abs(c[7999] - 0.5) > 1e-6) fails++;   // 末尾在保持段内
    }
    // 6. 应用包络
    {
        Synth s(8000);
        AudioBuf a = s.tone(1.0, WAVE_SINE, 440, 1.0);
        Envelope env;
        env.apply(a, 1.0);
        if (a.data[0] != 0) fails++;          // 起点静音
        if (a.data[a.frames() - 1] >= 20000) fails++;   // 末尾被包络衰减到 <0.5 原值
    }
    return fails;
}

} // namespace audx
} // namespace nefu
