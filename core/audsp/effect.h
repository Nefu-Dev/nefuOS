// ============================================================================
// nefuOS 音频DSP库 —— 效果器模块 (effect.h)
// ----------------------------------------------------------------------------
// 提供：
//   * Delay (带反馈/混响)
//   * Chorus (LFO 调制延时)
//   * Flanger (短延时 + 深反馈)
//   * Phaser (多级全通滤波级联)
//   * Schroeder Reverb (4 并联 comb + 2 串联 allpass)
//   * Distortion (软削波 tanh / 硬削波 / bitcrush)
//   * Compressor / Limiter (基于包络跟随)
//   * Tremolo (幅度 LFO)
//   * Ring Modulator (乘法调制)
// ============================================================================
#pragma once
#include <stdint.h>
#include "filter.h"
#include "env.h"

namespace nefu {
namespace audsp {

// ============================================================================
// DelayLine —— 通用延时线（可读写任意偏移）
// ============================================================================
struct DelayLine {
    double* buffer;
    int     size;
    int     write_idx;

    DelayLine();
    ~DelayLine();
    bool init(double seconds);
    void write_sample(double x);
    double read_delay(double delay_seconds) const;   // 线性插值读
};

// ============================================================================
// DelayEffect —— 反馈延时
// ============================================================================
struct DelayEffect {
    DelayLine line;
    double feedback;     // 0..0.9
    double mix;          // 0..1 干湿比
    void init(double delay_sec, double fb, double mix_);
    double process(double x);
};

// ============================================================================
// Chorus —— 合唱
// ----------------------------------------------------------------------------
// 用 LFO (0.5~2 Hz) 调制延时时间 (10~30ms)，产生多个失谐音。
// ============================================================================
struct Chorus {
    DelayLine line;
    double lfo_phase;
    double lfo_freq;       // Hz
    double min_delay;      // 秒
    double depth;          // 调制深度(秒)
    double mix;
    void init(double base_delay, double lfo_hz, double depth_sec, double mix_);
    double process(double x);
};

// ============================================================================
// Flanger —— 镶边 (短延时 1~10ms，深反馈)
// ============================================================================
struct Flanger {
    DelayLine line;
    double lfo_phase;
    double lfo_freq;
    double min_delay;
    double depth;
    double feedback;
    double mix;
    void init(double base_delay, double lfo_hz, double depth_sec, double fb, double mix_);
    double process(double x);
};

// ============================================================================
// Phaser —— 移相器
// ----------------------------------------------------------------------------
// N 级一阶全通，中心频率由 LFO 扫动。
// ============================================================================
#define PHASER_STAGES 6

struct Phaser {
    double stages[PHASER_STAGES];
    double lfo_phase;
    double lfo_freq;
    double min_fc, max_fc;
    double feedback;
    double mix;
    void init(double lfo_hz, double min_fc_, double max_fc_, double fb, double mix_);
    double process(double x);
};

// ============================================================================
// SchroederReverb —— 4 comb 并联 + 2 allpass 串联
// ============================================================================
#define REVERB_COMBS 4
#define REVERB_ALLPASSES 2

struct SchroederReverb {
    CombFilter combs[REVERB_COMBS];
    AllpassFilter allpasses[REVERB_ALLPASSES];
    double mix;             // 干湿比
    void init(double room_size, double mix_);
    double process(double x);
};

// ============================================================================
// Distortion —— 失真
// ============================================================================
enum DistortType {
    DIST_SOFT = 0,       // tanh 软削波
    DIST_HARD,           // 硬削波 [-1,1]
    DIST_BITCRUSH,       // 比特压缩
    DIST_FOLD            // 折叠波
};

struct Distortion {
    int    type;
    double drive;        // 输入增益
    int    bit_depth;    // bitcrush 用
    void init(int type_, double drive_);
    double process(double x);
};

// ============================================================================
// Compressor —— 压缩器/限制器
// ----------------------------------------------------------------------------
// 基于包络跟随检测电平平，超过阈值时按比例衰减。
// ============================================================================
struct Compressor {
    double threshold_db;   // 阈值 dB
    double ratio;          // 比率 (4:1 等；无穷大=limiter)
    double attack_ms;
    double release_ms;
    double makeup_db;
    EnvelopeFollower follower;
    bool   limiter;        // true=硬限制

    void init(double thr_db, double ratio_, double atk_ms, double rel_ms, double makeup);
    double process(double x);
};

// ============================================================================
// Tremolo ——  Tremolo（幅度 LFO）
// ============================================================================
struct Tremolo {
    double lfo_phase;
    double rate_hz;
    double depth;          // 0..1
    void init(double rate, double depth_);
    double process(double x);
};

// ============================================================================
// RingMod —— 环形调制
// ----------------------------------------------------------------------------
// out = x * sin(2*pi*freq*t)
// ============================================================================
struct RingMod {
    double phase;
    double freq;
    void init(double hz);
    double process(double x);
};

// ============================================================================
// 自检
// ============================================================================
int effect_self_test();

} // namespace audsp
} // namespace nefu
