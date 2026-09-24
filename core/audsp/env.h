// ============================================================================
// nefuOS 音频DSP库 —— 包络发生器模块 (env.h)
// ----------------------------------------------------------------------------
// 提供：
//   * ADSR 包络 (Attack / Decay / Sustain / Release)
//   * AHDSR 包络 (Attack-Hold-Decay-Sustain-Release)
//   * AR 包络 (Attack-Release, 常用于鼓类)
//   * 多段包络 (任意 (time,level) 节点折线)
//   * Envelope Follower (信号包络跟随器，用于侧链/自动增益)
//
// 包络状态机：
//   IDLE -> ATTACK -> DECAY -> SUSTAIN(等待 note off) -> RELEASE -> IDLE
// 所有时间参数单位为秒；输出值范围 [0,1]。
// ============================================================================
#pragma once
#include <stdint.h>

namespace nefu {
namespace audsp {

// ---- 包络状态枚举 ----
enum EnvState {
    ENV_IDLE = 0,
    ENV_ATTACK,
    ENV_HOLD,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE,
    ENV_DONE
};

// ============================================================================
// AdsrEnvelope —— 经典 ADSR
// ============================================================================
struct AdsrEnvelope {
    double attack;     // 攻击时间(秒)
    double decay;      // 衰减时间(秒)
    double sustain;    //  sustain 电平 [0,1]
    double release;    // 释放时间(秒)
    double level;      // 当前输出电平
    int    state;      // EnvState
    double samples_in_state;  // 当前状态已过采样数

    void init();
    void note_on();                 // 触发（按键按下）
    void note_off();                // 释放（按键松开）
    bool is_active() const;         // 是否仍在发声
    double process();               // 推进一个采样，返回当前电平
    void process_block(double* out, int n);
};

// ============================================================================
// AhdsrEnvelope —— AHDSR (多一个 Hold 阶段)
// ============================================================================
struct AhdsrEnvelope {
    double attack, hold, decay, sustain, release;
    double level;
    int    state;
    double samples_in_state;

    void init();
    void note_on();
    void note_off();
    bool is_active() const;
    double process();
    void process_block(double* out, int n);
};

// ============================================================================
// ArEnvelope —— Attack/Release (用于打击乐)
// ============================================================================
struct ArEnvelope {
    double attack;
    double release;
    double level;
    int    state;
    double samples_in_state;

    void init();
    void trigger();                 // 打一下就进入 AR
    double process();
};

// ============================================================================
// 多段包络 MidiEnvelope
// ----------------------------------------------------------------------------
// 由若干 (time_sec, level) 节点连成折线；note_on 后从节点 0 走到节点 N-1。
// 最多 MAX_POINTS 个节点。
// ============================================================================
#define ENV_MAX_POINTS 16

struct SegEnvPoint {
    double time;     // 距起点的时间(秒)
    double level;    // 该点电平 [0,1]
};

struct MultiSegmentEnv {
    SegEnvPoint points[ENV_MAX_POINTS];
    int    npoints;
    double level;
    int    seg_index;            // 当前正在走的线段
    double elapsed;              // 自 note_on 起经过的时间
    bool   running;

    void init();
    void add_point(double time, double level);
    void note_on();
    double process();            // 走一个采样；走到末尾后保持最后电平
};

// ============================================================================
// EnvelopeFollower —— 包络跟随器
// ----------------------------------------------------------------------------
// 对输入信号取绝对值后做一阶平滑：
//   fast  attack 系数 (信号上升时快跟)
//   slow  release 系数 (信号下降时慢落)
// 常用于 sidechain 压缩 / 自动电平检测。
// ============================================================================
struct EnvelopeFollower {
    double attack_coeff;   // 上升系数 (0..1)
    double release_coeff;  // 下降系数 (0..1)
    double envelope;       // 当前包络值

    void init(double attack_ms, double release_ms);
    double process(double input);
    void process_block(const double* in, double* out, int n);
};

// ============================================================================
// 自检
// ============================================================================
int env_self_test();

} // namespace audsp
} // namespace nefu
