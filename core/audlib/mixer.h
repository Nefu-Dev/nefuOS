// nefuOS audlib —— 混音器 mixer 与效果 delay/reverb
// 教学版：多轨混音（增益/淡入淡出/混音）与延时/混响效果器。
#pragma once
#include <vector>
#include "audlib/wave.h"

namespace nefu {
namespace audx {

// 混音器：多轨 -> 立体声总线
class Mixer {
public:
    Mixer();

    // 添加一轨（自动匹配采样率；与总线不同则跳过）
    void add_track(const AudioBuf& track, double gain);
    // 清空
    void clear();
    // 混音：所有轨相加（限制在 16-bit 范围），返回单声道总线
    AudioBuf mix(int sample_rate) const;
    // 混音并归一化（峰值拉到 0.9，防削波）
    AudioBuf mix_normalized(int sample_rate) const;

    int track_count() const { return (int)tracks.size(); }

    // ---- self test ----
    static int self_test();

private:
    struct Track { AudioBuf buf; double gain; };
    std::vector<Track> tracks;
};

// 延时效果器（echo）
class Delay {
public:
    Delay() : delay_ms(250), feedback(0.4), mix(0.5) {}
    // 参数：延时毫秒、反馈（0..0.9）、干湿比
    void set(double ms, double fb, double mix_ratio);
    // 处理（单声道；就地）
    void process(AudioBuf& a);
    // 静态处理（不改变对象状态）
    static AudioBuf apply(const AudioBuf& in, double ms, double fb, double mix_ratio);

    // ---- self test ----
    static int self_test();

private:
    double delay_ms, feedback, mix;
};

// 简单混响（多个梳状滤波近似）
class Reverb {
public:
    Reverb() : room(0.5), damp(0.3) {}
    void set(double room_size, double damping);
    // 处理
    void process(AudioBuf& a);
    static AudioBuf apply(const AudioBuf& in, double room_size, double damping);

    static int self_test();

private:
    double room, damp;
};

} // namespace audx
} // namespace nefu
