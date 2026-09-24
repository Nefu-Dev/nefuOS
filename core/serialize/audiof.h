// nefuOS 数据序列化与编解码库 —— 音频格式模块
// audiof.h: WAV / AIFF / AU / raw PCM
//
// 内存统一表示为 AudioBuf：
//   - 采样率 hz、声道数 ch、每样本位数 bits(8/16/24/32)
//   - samples 按 interleaved 存储（L,R,L,R...），32 位 float，范围 [-1,1]
// WAV 支持 PCM 8/16/24/32 位与 IEEE float；AIFF/AU 只读常见子集。
#pragma once
#include "../klib/klib.h"

namespace nefu {
namespace serialize {

// 统一音频缓冲：interleaved float，范围 [-1,1]
struct AudioBuf {
    int hz;          // 采样率，如 44100
    int channels;    // 声道数（1 单声道 / 2 立体声）
    int frames;      // 帧数（每帧包含所有声道）
    float* samples;  // 交错采样（拥有）
    bool own;

    AudioBuf() : hz(44100), channels(1), frames(0), samples(0), own(false) {}
    void alloc(int rate, int ch, int nframes);
    void free_buf();
    float get(int frame, int ch) const;        // 取某帧某声道
    void  set(int frame, int ch, float v);
    int   total_samples() const { return frames * channels; }
};

// ============================================================================
// WAV（RIFF/WAVE）
//   "RIFF"(4)+size(4)+"WAVE"(4)+ "fmt "(chunk)+"data"(chunk)+PCM 数据
//   支持 PCM 8/16/24/32 位与 IEEE float。
// ============================================================================
struct WavInfo {
    int format;       // 1=PCM, 3=IEEE float
    int channels;
    int sample_rate;
    int byte_rate;
    int block_align;
    int bits;
    int data_bytes;    // PCM 数据字节数
};

bool   wav_read_info(const uint8_t* data, int len, WavInfo& info);
bool   wav_read(const uint8_t* data, int len, AudioBuf& out);
int    wav_write(const AudioBuf& audio, uint8_t* out, int out_cap, int bits = 16);
int    wav_encoded_size(const AudioBuf& audio, int bits = 16);
int    wav_self_test();

// ============================================================================
// AIFF（Apple Interchange File Format，大端）——只读常见 PCM 子集
// ============================================================================
struct AiffInfo {
    int channels;
    int frames;
    int sample_rate;
    int bits;
};
bool aiff_read(const uint8_t* data, int len, AudioBuf& out);
int  aiff_self_test();

// ============================================================================
// AU（Sun/NeXT audio，大端）——读/写
//   头24字节：".snd"(4)+hdr_size(4)+data_size(4)+encoding(4)+rate(4)+ch(4)
// ============================================================================
bool au_read(const uint8_t* data, int len, AudioBuf& out);
int  au_write(const AudioBuf& audio, uint8_t* out, int out_cap);
int  au_encoded_size(const AudioBuf& audio);
int  au_self_test();

// ============================================================================
// raw PCM 工具：int16 <-> float 转换
// ============================================================================
void   raw_i16_to_float(const int16_t* in, float* out, int n);
void   raw_float_to_i16(const float* in, int16_t* out, int n);
int    audiof_self_test();

} // namespace serialize
} // namespace nefu

// audiof.h 汇总：
//   - WAV (PCM 8/16/24/32 位 + IEEE float) 读写与信息读取
//   - AIFF 读取（只读）
//   - AU (Sun/NeXT) 读写
//   - raw PCM 与 float/i16 互转工具
//   - AudioBuf 交错采样缓冲，alloc/free_buf/get/set
// 所有函数失败返回 false 或 0，不抛异常。