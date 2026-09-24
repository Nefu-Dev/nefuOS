// audio_engine.h —— 音频引擎：音效播放、混音、音量/声道、循环、淡入淡出、PCM 缓冲、合成器
//
// 纯整数 PCM 运算（16-bit 有符号采样），不依赖 FPU。
// 实际出声由 platform_play_wav_mem 负责；本模块负责合成与混音逻辑，可独立自测。
#pragma once

#include <stdint.h>
#include "ge_math.h"
#include "../klib/klib.h"

namespace nefu {
namespace gameengine {

// 波形类型（合成器）
enum WaveForm {
    WAVE_SINE = 0,
    WAVE_SQUARE,
    WAVE_SAWTOOTH,
    WAVE_TRIANGLE,
    WAVE_NOISE,
};

// ============================================================================
//  AudioSample —— 一段 16-bit PCM 单声道采样
// ============================================================================
struct AudioSample {
    int16_t* data;       // PCM 数据（本对象持有所有权）
    int      length;     // 采样数
    int      rate;       // 采样率（Hz）

    AudioSample() : data(0), length(0), rate(22050) {}
    ~AudioSample() { if (data) delete[] data; data = 0; }

    void alloc(int n, int r) {
        if (data) delete[] data;
        length = n; rate = r;
        data = new int16_t[n > 0 ? n : 1];
        // 手动清零（规避 MinGW -O2 memset 误优化）
        if (data) for (int i = 0; i < n; i++) data[i] = 0;
    }
};

// ============================================================================
//  AudioVoice —— 一个正在播放的声音实例
// ============================================================================
struct AudioVoice {
    const AudioSample* sample;
    int   position;       // 当前采样指针
    int   volume;         // 0..255
    int   pan;           // -128(左)..127(右)
    bool  loop;
    bool  playing;
    int   fade_ms;       // 剩余淡入淡出时间
    int   fade_total;
    int   fade_target_vol;

    AudioVoice() : sample(0), position(0), volume(255), pan(0),
                   loop(false), playing(false),
                   fade_ms(0), fade_total(0), fade_target_vol(255) {}

    void play(const AudioSample* s, int vol, bool lp) {
        sample = s; position = 0; volume = vol; loop = lp;
        playing = true; fade_ms = 0;
    }

    // 淡入到目标音量（ms）
    void fade_to(int target_vol, int ms) {
        fade_target_vol = target_vol;
        fade_total = ms > 0 ? ms : 1;
        fade_ms = ms;
    }

    // 取一帧（单声道混合到立体声左右）；返回 false 表示播完
    bool read_sample(int16_t& out_left, int16_t& out_right, int step_ms);
};

// ============================================================================
//  Synthesizer —— 合成器：用振荡器 + ADSR 生成音效
// ============================================================================
struct Synthesizer {
    // 生成一个简单音调（带 ADSR 包络）到 out。
    // freq_hz：频率；dur_ms：时长；wave：波形；vol 0..255。
    static void tone(AudioSample& out, int freq_hz, int dur_ms,
                     WaveForm wave, int vol);

    // 生成噪声爆发（爆炸/击打）
    static void noise(AudioSample& out, int dur_ms, int vol, uint32_t seed);

    // 生成滑音（上升，如得分）
    static void sweep(AudioSample& out, int from_hz, int to_hz, int dur_ms,
                      WaveForm wave, int vol);
};

// ============================================================================
//  AudioMixer —— 混音器：把多路 voice 混成一条立体声音流
// ============================================================================
const int GE_MAX_VOICES = 8;

struct AudioMixer {
    AudioVoice voices[GE_MAX_VOICES];
    int  master_volume;     // 0..255
    int  rate;

    AudioMixer() : master_volume(200), rate(22050) {}

    int free_voice() {
        for (int i = 0; i < GE_MAX_VOICES; i++)
            if (!voices[i].playing) return i;
        return -1;
    }

    int play(const AudioSample* s, int vol = 255, bool loop = false) {
        int idx = free_voice();
        if (idx < 0) return -1;
        voices[idx].play(s, vol, loop);
        return idx;
    }

    void stop(int idx) {
        if (idx >= 0 && idx < GE_MAX_VOICES) voices[idx].playing = false;
    }

    // 混合 n 帧到 out（立体声交织 L,R）；返回实际写入帧数
    int mix(int16_t* out, int frames, int dt_ms);
};

// ============================================================================
//  自测
// ============================================================================

// ============================================================================
//  MusicSequencer —— 简易步进音序器：按节拍调度音符
// ============================================================================
struct MusicSequencer {
    int  bpm;              // 每分钟节拍数
    int  step;             // 当前步
    int  steps_per_bar;
    uint32_t next_note_at; // 下一个音符时间（ms）
    bool playing;

    // 简单音名表：C4..B4 的频率
    static int note_freq(int midi) {
        // midi 60 = C4 = 261Hz；等比 2^(1/12)，用查表近似
        static const int freqs[12] = {
            262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
        };
        if (midi < 0) midi = 0;
        int oct = midi / 12;
        int semi = midi % 12;
        int f = freqs[semi];
        while (oct > 5) { f *= 2; oct--; }   // MIDI 60 = C4 = oct 5
        while (oct < 5) { f /= 2; oct++; }
        return f;
    }

    MusicSequencer() : bpm(120), step(0), steps_per_bar(16),
                       next_note_at(0), playing(false) {}

    int step_ms() const { return 60000 / bpm / 4; }   // 16 分音符

    void start() { playing = true; step = 0; next_note_at = 0; }
    void stop()  { playing = false; }

    // 推进；返回当前步应播放的 MIDI 音符（-1 = 休止）
    int update(int now_ms, const int* pattern, int pattern_len) {
        if (!playing) return -1;
        if (now_ms < next_note_at) return -1;
        int note = pattern[step % pattern_len];
        next_note_at = now_ms + step_ms();
        step++;
        return note;
    }
};

// ============================================================================
//  DelayNode —— 简单回声效果（环形缓冲）
// ============================================================================
const int GE_DELAY_MAX = 4096;
struct DelayNode {
    int16_t buffer[GE_DELAY_MAX];
    int   write_pos;
    int   delay_samples;
    int   feedback;      // 0..255 反馈量
    int   mix;          // 0..255 干湿比

    DelayNode() : write_pos(0), delay_samples(1024), feedback(40), mix(30) {
        for (int i = 0; i < GE_DELAY_MAX; i++) buffer[i] = 0;
    }

    // 处理一帧输入，输出带回声的样本
    int16_t process(int16_t in) {
        int read_pos = write_pos - delay_samples;
        if (read_pos < 0) read_pos += GE_DELAY_MAX;
        int16_t delayed = buffer[read_pos];
        // 反馈：输出回写并衰减
        int32_t fb = (int32_t)delayed * feedback / 255;
        buffer[write_pos] = (int16_t)(in + fb);
        write_pos++;
        if (write_pos >= GE_DELAY_MAX) write_pos = 0;
        // 干湿混合
        return (int16_t)((in * (255 - mix) + delayed * mix) / 255);
    }
    void reset() {
        for (int i = 0; i < GE_DELAY_MAX; i++) buffer[i] = 0;
        write_pos = 0;
    }
};

// ============================================================================
//  LowPassFilter —— 单极点低通滤波器（柔化高频）
// ============================================================================
struct LowPassFilter {
    int16_t prev;
    int     alpha;     // 0..255，越大越保留高频

    LowPassFilter() : prev(0), alpha(128) {}

    int16_t process(int16_t in) {
        // out = prev + alpha*(in - prev)/255
        int32_t diff = in - prev;
        prev = (int16_t)(prev + diff * alpha / 255);
        return prev;
    }
    void reset() { prev = 0; }
};

// ============================================================================
//  NoiseGenerator —— 白噪声/粉噪声合成
// ============================================================================
struct NoiseGenerator {
    uint32_t state;
    int      type;      // 0=白噪声, 1=粉噪声
    int      b0,b1,b2,b3,b4,b5,b6;  // 粉噪声滤波状态

    NoiseGenerator() : state(0x12345678), type(0),
                       b0(0),b1(0),b2(0),b3(0),b4(0),b5(0),b6(0) {}

    // LCG 白噪声 -> -32768..32767
    int16_t next_white() {
        state = state * 1664525u + 1013904223u;
        return (int16_t)(state >> 16);
    }
    // 粉噪声（Paul Kellet 滤波器近似）
    int16_t next_pink() {
        int16_t w = next_white();
        b0 = (b0 + w) >> 1;
        b1 = (b1 + w) >> 1;
        b2 = (b2 + w) >> 1;
        return (int16_t)(b0 + b1 + b2);
    }
    int16_t next() { return type == 0 ? next_white() : next_pink(); }
};

// ============================================================================
//  DrumSequencer —— 简易鼓点音序器
// ============================================================================
struct DrumSequencer {
    int pattern[16];     // 每步: 0=空,1=底鼓,2=军鼓,3=踩镲
    int step;
    int bpm;
    int step_ms;
    int elapsed;

    DrumSequencer() : step(0), bpm(120), elapsed(0) {
        for (int i = 0; i < 16; i++) pattern[i] = 0;
        step_ms = 60000 / bpm / 4;   // 16 分音符
    }
    // 返回当前步应触发的鼓类型（0=无）
    int update(int dt_ms) {
        elapsed += dt_ms;
        if (elapsed < step_ms) return 0;
        elapsed -= step_ms;
        int hit = pattern[step];
        step = (step + 1) & 15;
        return hit;
    }
};
int audio_engine_self_test();

} // namespace gameengine
} // namespace nefu
