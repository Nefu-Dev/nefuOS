// nefuOS audlib —— WAV 文件读写 wave
// 教学版：读写 16-bit PCM WAV 文件（单声道/双声道），用于音频教学与合成输出。
// 类 + STL + 中文注释。
#pragma once
#include <vector>
#include <string>

namespace nefu {
namespace audx {

// 单条音频（16-bit PCM，-32768..32767）
struct AudioBuf {
    int sample_rate;      // 采样率（如 44100）
    int channels;         // 声道数（1 单声道 / 2 双声道）
    std::vector<short> data;   // 采样交错存储：L,R,L,R...

    AudioBuf() : sample_rate(44100), channels(1) {}
    AudioBuf(int rate, int ch) : sample_rate(rate), channels(ch) {}

    // 采样数（每声道）
    int frames() const { return channels > 0 ? (int)data.size() / channels : 0; }
    // 时长（秒）
    double duration() const { return sample_rate > 0 ? (double)frames() / sample_rate : 0; }
    // 设置单声道帧值
    void set_frame(int i, short v) { if (channels == 1 && i >= 0 && i < frames()) data[i] = v; }
    // 获取单声道帧值（多声道取第一声道）
    short frame(int i) const { return (i >= 0 && i < frames()) ? data[i * channels] : 0; }
    // 归一化输出（0..1）
    double frame_norm(int i) const { return (double)frame(i) / 32768.0; }
    // 生成静音
    void silence(int n_frames) { data.assign((size_t)n_frames * channels, 0); }
    // 淡入淡出（前/后 n 帧）
    void fade_in(int n);
    void fade_out(int n);
};

// WAV 读写器（16-bit PCM，RIFF 格式）
class WaveIO {
public:
    // 从文件读取（成功返回 true）
    static bool read(const std::string& path, AudioBuf& out);
    // 写入文件（成功返回 true）
    static bool write(const std::string& path, const AudioBuf& in);
    // 由纯数据生成 WAV 字节（便于内存使用）
    static std::vector<unsigned char> to_bytes(const AudioBuf& in);
    // 从字节解析 WAV（成功返回 true）
    static bool from_bytes(const std::vector<unsigned char>& bytes, AudioBuf& out);

    // ---- self test ----
    static int self_test();
};

} // namespace audx
} // namespace nefu
