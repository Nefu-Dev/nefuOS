// ============================================================================
// nefuOS 音频DSP库 —— 合成器模块 (synth.h)
// ----------------------------------------------------------------------------
// 提供：
//   * SubtractiveVoice: 减法合成 voice (osc + filter + amp env)
//   * FmSynth: 4 算子 FM 合成 (DX7 风格算法)
//   * AdditiveSynth: 加法合成 (多个正弦谐波叠加)
//   * DrumSynth: 鼓合成 (kick / snare / hihat)
//   * VoiceAllocator: 8-voice 复音分配器
//   * Mixer: 多通道混音器
//
// 频率换算：MIDI note (0..127) -> Hz：440 * 2^((m-69)/12)
// ============================================================================
#pragma once
#include <stdint.h>
#include "osc.h"
#include "env.h"
#include "filter.h"

namespace nefu {
namespace audsp {

// ---- MIDI note 转频率 ----
double midi_to_hz(int note);

// ============================================================================
// SubtractiveVoice —— 减法合成 voice
// ============================================================================
struct SubtractiveVoice {
    Oscillator osc;
    Biquad     filter;
    AdsrEnvelope amp_env;
    AdsrEnvelope filter_env;
    double filter_env_amount;   // 包络对截止的调制量
    double base_cutoff;
    double last_cutoff;          // 上次计算系数的截止频率（避免每采样重置状态）
    bool   active;
    int    note;

    void init();
    void note_on(int midi_note, int velocity);
    void note_off();
    double process();
    bool is_active() const { return active; }
};

// ============================================================================
// FmSynthVoice —— 4 算子 FM voice
// ----------------------------------------------------------------------------
// 算法 4  (DX7): OP1 是 carrier，OP2/OP3/OP4 是 modulator。
// 简化为：mod4 -> mod3 -> mod2 -> carrier1
// ============================================================================
struct FmSynthVoice {
    FmOperator op[4];
    AdsrEnvelope env[4];
    double ratios[4];       // 频率比
    double mods[4];         // 调制深度
    bool   active;
    int    note;

    void init();
    void note_on(int midi_note, int velocity);
    void note_off();
    double process();
    bool is_active() const { return active; }
};

// ============================================================================
// AdditiveVoice —— 加法合成
// ----------------------------------------------------------------------------
// 最多 N_HARM 个正弦谐波，各自有增益。
// ============================================================================
#define ADDITIVE_MAX_HARM 16

struct AdditiveVoice {
    Oscillator harm[ADDITIVE_MAX_HARM];
    double gain[ADDITIVE_MAX_HARM];
    int    nharm;
    bool   active;

    void init();
    void note_on(int midi_note);
    void note_off();
    double process();
};

// ============================================================================
// DrumSynth —— 鼓合成器
// ============================================================================
enum DrumType {
    DRUM_KICK = 0,
    DRUM_SNARE,
    DRUM_HIHAT
};

struct DrumSynth {
    Oscillator osc;
    NoiseGenerator noise;
    ArEnvelope env;
    Biquad     hp;
    bool       playing;
    double     phase;

    void init();
    void trigger(int type);
    double process();
};

// ============================================================================
// VoiceAllocator —— 8 复音分配器
// ============================================================================
#define MAX_VOICES 8

struct VoiceAllocator {
    SubtractiveVoice voices[MAX_VOICES];
    int  last_used;

    void init();
    void note_on(int midi_note, int velocity);
    void note_off(int midi_note);
    double process();            // 所有 voice 相加
    int  active_count() const;
};

// ============================================================================
// Mixer —— 多通道混音器
// ============================================================================
struct Mixer {
    double* gains;       // 每通道增益
    int     n_ch;
    double  master;

    Mixer();
    void init(int n_channels);
    ~Mixer();
    void set_gain(int ch, double g);
    void set_master(double m);
    double process(const double* inputs);   // inputs[n_ch]
};

// ============================================================================
// 自检
// ============================================================================
int synth_self_test();

} // namespace audsp
} // namespace nefu
