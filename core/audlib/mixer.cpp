// nefuOS audlib —— 混音器与效果器实现 + 自测
#include "audlib/mixer.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace nefu {
namespace audx {

Mixer::Mixer() {}

void Mixer::add_track(const AudioBuf& track, double gain) {
    Track t;
    t.buf = track;
    t.gain = gain;
    tracks.push_back(t);
}

void Mixer::clear() { tracks.clear(); }

AudioBuf Mixer::mix(int sample_rate) const {
    // 求最长轨
    int maxf = 0;
    for (size_t i = 0; i < tracks.size(); i++)
        if (tracks[i].buf.frames() > maxf) maxf = tracks[i].buf.frames();
    AudioBuf out(sample_rate, 1);
    out.silence(maxf);
    for (size_t i = 0; i < tracks.size(); i++) {
        const AudioBuf& t = tracks[i].buf;
        if (t.sample_rate != sample_rate) continue;   // 采样率不匹配跳过
        int n = t.frames();
        for (int f = 0; f < n; f++) {
            int v = out.data[f] + (int)(t.frame(f) * tracks[i].gain);
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            out.data[f] = (short)v;
        }
    }
    return out;
}

AudioBuf Mixer::mix_normalized(int sample_rate) const {
    AudioBuf out = mix(sample_rate);
    short peak = 0;
    for (int i = 0; i < out.frames(); i++) {
        short v = out.data[i];
        if (v < 0) v = (short)(-v);
        if (v > peak) peak = v;
    }
    if (peak == 0) return out;
    double g = 0.9 * 32767.0 / peak;
    for (int i = 0; i < out.frames(); i++)
        out.data[i] = (short)(out.data[i] * g);
    return out;
}

// ---- self test ----
int Mixer::self_test() {
    int fails = 0;
    AudioBuf a(8000, 1), b(8000, 1);
    a.silence(10); b.silence(10);
    a.data[0] = 1000; a.data[1] = 2000;
    b.data[0] = 500;  b.data[2] = 3000;
    // 1. 混音求和
    {
        Mixer m;
        m.add_track(a, 1.0);
        m.add_track(b, 1.0);
        AudioBuf o = m.mix(8000);
        if (o.frames() != 10) fails++;
        if (o.data[0] != 1500) fails++;   // 1000+500
        if (o.data[1] != 2000) fails++;   // 2000+0
        if (o.data[2] != 3000) fails++;   // 0+3000
    }
    // 2. 增益
    {
        Mixer m;
        m.add_track(a, 0.5);
        AudioBuf o = m.mix(8000);
        if (o.data[0] != 500) fails++;
    }
    // 3. 削波保护
    {
        AudioBuf big(8000, 1);
        big.silence(5);
        big.data[0] = 30000;
        Mixer m;
        m.add_track(big, 1.0);
        m.add_track(big, 1.0);   // 60000 应削到 32767
        AudioBuf o = m.mix(8000);
        if (o.data[0] != 32767) fails++;
    }
    // 4. 归一化
    {
        AudioBuf small(8000, 1);
        small.silence(5);
        small.data[0] = 100;
        Mixer m;
        m.add_track(small, 1.0);
        AudioBuf o = m.mix_normalized(8000);
        if (o.data[0] < 29000 || o.data[0] > 30000) fails++;   // ~0.9*32767
    }
    // 5. 清空
    {
        Mixer m;
        m.add_track(a, 1.0);
        m.clear();
        if (m.track_count() != 0) fails++;
    }
    return fails;
}

void Delay::set(double ms, double fb, double mix_ratio) {
    delay_ms = ms; feedback = fb; mix = mix_ratio;
    if (feedback > 0.9) feedback = 0.9;
    if (mix < 0) mix = 0;
    if (mix > 1) mix = 1;
}

AudioBuf Delay::apply(const AudioBuf& in, double ms, double fb, double mix_ratio) {
    AudioBuf out = in;
    int delay_frames = (int)(ms * in.sample_rate / 1000.0);
    // 输出 = 干 + 湿（延时 + 反馈）
    std::vector<double> wet(in.frames(), 0);
    for (int i = 0; i < in.frames(); i++) {
        double w = 0;
        if (i >= delay_frames) w = wet[i - delay_frames];
        wet[i] = in.frame(i) * 0.0 + w * fb;   // 反馈环
        if (i >= delay_frames) wet[i] += in.frame(i - delay_frames);
        double v = in.frame(i) * (1 - mix_ratio) + wet[i] * mix_ratio;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out.data[i] = (short)v;
    }
    return out;
}

void Delay::process(AudioBuf& a) { a = apply(a, delay_ms, feedback, mix); }

// ---- self test ----
int Delay::self_test() {
    int fails = 0;
    AudioBuf a(1000, 1);
    a.silence(1000);
    a.data[0] = 1000;
    // 1. 延时 100ms：第 100 帧出现回声
    {
        AudioBuf o = Delay::apply(a, 100, 0.0, 0.5);
        if (o.data[99] != 0) fails++;     // 回声之前无信号
        if (o.data[100] != 500) fails++;  // 1000*0.5 延迟 100 帧
    }
    // 2. 干湿比
    {
        AudioBuf o = Delay::apply(a, 100, 0.0, 1.0);
        if (o.data[0] != 0) fails++;      // 纯湿：起点无干声
        if (o.data[100] != 1000) fails++;
    }
    // 3. 反馈产生后续回声（回声在 50 帧的倍数处）
    {
        AudioBuf o = Delay::apply(a, 50, 0.5, 0.5);
        if (o.data[50] != 500) fails++;   // 第一回声 1000*0.5
        if (o.data[100] != 250) fails++;  // 第二回声 1000*0.5*0.5
    }
    return fails;
}

void Reverb::set(double room_size, double damping) {
    room = room_size; damp = damping;
    if (room < 0) room = 0;
    if (room > 0.95) room = 0.95;
    if (damp < 0) damp = 0;
    if (damp > 0.9) damp = 0.9;
}

AudioBuf Reverb::apply(const AudioBuf& in, double room_size, double damping) {
    AudioBuf out = in;
    int rate = in.sample_rate;
    // 4 个不同延时长度（采样）
    int d1 = (int)(0.03 * rate * (0.5 + room_size));
    int d2 = (int)(0.045 * rate * (0.5 + room_size));
    int d3 = (int)(0.062 * rate * (0.5 + room_size));
    int d4 = (int)(0.09 * rate * (0.5 + room_size));
    std::vector<double> b1(in.frames()), b2(in.frames()), b3(in.frames()), b4(in.frames());
    for (int i = 0; i < in.frames(); i++) {
        double x = in.frame(i);
        // 梳状滤波：输出 = 输入延迟 d 帧 + 反馈
        if (i >= d1) b1[i] = in.frame(i - d1) + b1[i - d1] * damping;
        if (i >= d2) b2[i] = in.frame(i - d2) + b2[i - d2] * damping;
        if (i >= d3) b3[i] = in.frame(i - d3) + b3[i - d3] * damping;
        if (i >= d4) b4[i] = in.frame(i - d4) + b4[i - d4] * damping;
        double wet = (b1[i] + b2[i] + b3[i] + b4[i]) * 0.25;
        double v = x * 0.5 + wet * 0.5;
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        out.data[i] = (short)v;
    }
    return out;
}

void Reverb::process(AudioBuf& a) { a = apply(a, room, damp); }

// ---- self test ----
int Reverb::self_test() {
    int fails = 0;
    AudioBuf a(1000, 1);
    a.silence(200);
    a.data[0] = 8000;
    // 1. 输入脉冲产生尾部
    {
        AudioBuf o = Reverb::apply(a, 0.5, 0.3);
        bool tail = false;
        for (int i = 100; i < o.frames(); i++)
            if (o.data[i] != 0) tail = true;
        if (!tail) fails++;
    }
    // 2. 干声保留
    {
        AudioBuf o = Reverb::apply(a, 0.5, 0.3);
        if (o.data[0] != 4000) fails++;   // x*0.5
    }
    // 3. 混响不越界
    {
        AudioBuf o = Reverb::apply(a, 0.8, 0.5);
        for (int i = 0; i < o.frames(); i++) {
            if (o.data[i] > 32767 || o.data[i] < -32768) fails++;
        }
    }
    return fails;
}

} // namespace audx
} // namespace nefu
