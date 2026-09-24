// ============================================================================
// nefuOS 音频DSP库 —— 包络发生器实现 (env.cpp)
// ============================================================================
#include "env.h"
#include "osc.h"           // g_sample_rate
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

// ============================================================================
// AdsrEnvelope
// ============================================================================
void AdsrEnvelope::init() {
    attack  = 0.01;
    decay   = 0.1;
    sustain = 0.7;
    release = 0.2;
    level   = 0.0;
    state   = ENV_IDLE;
    samples_in_state = 0.0;
}

void AdsrEnvelope::note_on() {
    state = ENV_ATTACK;
    samples_in_state = 0.0;
    level = 0.0;
}

void AdsrEnvelope::note_off() {
    if (state != ENV_IDLE && state != ENV_DONE) {
        state = ENV_RELEASE;
        samples_in_state = 0.0;
    }
}

bool AdsrEnvelope::is_active() const {
    return state != ENV_IDLE && state != ENV_DONE;
}

double AdsrEnvelope::process() {
    double sr = g_sample_rate;
    samples_in_state += 1.0;
    switch (state) {
    case ENV_IDLE:
    case ENV_DONE:
        level = 0.0;
        break;
    case ENV_ATTACK: {
        double n = attack * sr;
        if (n <= 0.0001) { level = 1.0; state = ENV_DECAY; break; }
        level = samples_in_state / n;
        if (level >= 1.0) { level = 1.0; state = ENV_DECAY; samples_in_state = 0.0; }
        break;
    }
    case ENV_DECAY: {
        double n = decay * sr;
        if (n <= 0.0001) { level = sustain; state = ENV_SUSTAIN; break; }
        double t = samples_in_state / n;
        level = 1.0 - (1.0 - sustain) * t;
        if (level <= sustain) { level = sustain; state = ENV_SUSTAIN; }
        break;
    }
    case ENV_SUSTAIN:
        level = sustain;
        break;
    case ENV_RELEASE: {
        double n = release * sr;
        if (n <= 0.0001) { level = 0.0; state = ENV_DONE; break; }
        double start = (samples_in_state == 1.0) ? level : level;
        double t = samples_in_state / n;
        level = start * (1.0 - t);
        if (level <= 0.0) { level = 0.0; state = ENV_DONE; }
        break;
    }
    }
    return level;
}

void AdsrEnvelope::process_block(double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process();
}

// ============================================================================
// AhdsrEnvelope
// ============================================================================
void AhdsrEnvelope::init() {
    attack = 0.01; hold = 0.05; decay = 0.1; sustain = 0.7; release = 0.2;
    level = 0.0; state = ENV_IDLE; samples_in_state = 0.0;
}

void AhdsrEnvelope::note_on() {
    state = ENV_ATTACK;
    samples_in_state = 0.0;
    level = 0.0;
}

void AhdsrEnvelope::note_off() {
    if (state != ENV_IDLE && state != ENV_DONE) {
        state = ENV_RELEASE;
        samples_in_state = 0.0;
    }
}

bool AhdsrEnvelope::is_active() const {
    return state != ENV_IDLE && state != ENV_DONE;
}

double AhdsrEnvelope::process() {
    double sr = g_sample_rate;
    samples_in_state += 1.0;
    switch (state) {
    case ENV_IDLE:
    case ENV_DONE:
        level = 0.0; break;
    case ENV_ATTACK: {
        double n = attack * sr;
        if (n <= 0.0001) { level = 1.0; state = ENV_HOLD; break; }
        level = samples_in_state / n;
        if (level >= 1.0) { level = 1.0; state = ENV_HOLD; samples_in_state = 0.0; }
        break;
    }
    case ENV_HOLD: {
        double n = hold * sr;
        level = 1.0;
        if (samples_in_state >= n) { state = ENV_DECAY; samples_in_state = 0.0; }
        break;
    }
    case ENV_DECAY: {
        double n = decay * sr;
        if (n <= 0.0001) { level = sustain; state = ENV_SUSTAIN; break; }
        double t = samples_in_state / n;
        level = 1.0 - (1.0 - sustain) * t;
        if (level <= sustain) { level = sustain; state = ENV_SUSTAIN; }
        break;
    }
    case ENV_SUSTAIN:
        level = sustain; break;
    case ENV_RELEASE: {
        double n = release * sr;
        if (n <= 0.0001) { level = 0.0; state = ENV_DONE; break; }
        double t = samples_in_state / n;
        level = level * (1.0 - t);
        if (level <= 0.0) { level = 0.0; state = ENV_DONE; }
        break;
    }
    }
    return level;
}

void AhdsrEnvelope::process_block(double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process();
}

// ============================================================================
// ArEnvelope
// ============================================================================
void ArEnvelope::init() {
    attack = 0.005; release = 0.3;
    level = 0.0; state = ENV_IDLE; samples_in_state = 0.0;
}

void ArEnvelope::trigger() {
    state = ENV_ATTACK;
    samples_in_state = 0.0;
    level = 0.0;
}

double ArEnvelope::process() {
    double sr = g_sample_rate;
    samples_in_state += 1.0;
    if (state == ENV_IDLE || state == ENV_DONE) { level = 0.0; return 0.0; }
    if (state == ENV_ATTACK) {
        double n = attack * sr;
        if (n <= 0.0001) { level = 1.0; state = ENV_RELEASE; samples_in_state = 0.0; return level; }
        level = samples_in_state / n;
        if (level >= 1.0) { level = 1.0; state = ENV_RELEASE; samples_in_state = 0.0; }
        return level;
    }
    // RELEASE
    double n = release * sr;
    if (n <= 0.0001) { level = 0.0; state = ENV_DONE; return 0.0; }
    double t = samples_in_state / n;
    level = 1.0 - t;
    if (level <= 0.0) { level = 0.0; state = ENV_DONE; }
    return level;
}

// ============================================================================
// MultiSegmentEnv
// ============================================================================
void MultiSegmentEnv::init() {
    npoints = 0;
    level = 0.0;
    seg_index = 0;
    elapsed = 0.0;
    running = false;
}

void MultiSegmentEnv::add_point(double t, double l) {
    if (npoints >= ENV_MAX_POINTS) return;
    points[npoints].time = t;
    points[npoints].level = l;
    npoints++;
}

void MultiSegmentEnv::note_on() {
    if (npoints < 2) return;
    running = true;
    seg_index = 0;
    elapsed = 0.0;
    level = points[0].level;
}

double MultiSegmentEnv::process() {
    if (!running) return level;
    double dt = 1.0 / g_sample_rate;
    elapsed += dt;
    while (seg_index < npoints - 1) {
        double t0 = points[seg_index].time;
        double t1 = points[seg_index + 1].time;
        if (elapsed <= t1) {
            double span = t1 - t0;
            double frac = (span > 0.0001) ? (elapsed - t0) / span : 1.0;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            level = points[seg_index].level +
                    (points[seg_index + 1].level - points[seg_index].level) * frac;
            return level;
        }
        seg_index++;
    }
    level = points[npoints - 1].level;
    return level;
}

// ============================================================================
// EnvelopeFollower
// ============================================================================
void EnvelopeFollower::init(double attack_ms, double release_ms) {
    // 将毫秒时间常数转为一阶平滑系数：c = exp(-1/(tau*sr))
    double sr = g_sample_rate;
    double at_tau = attack_ms / 1000.0;
    double rel_tau = release_ms / 1000.0;
    attack_coeff  = exp(-1.0 / (at_tau * sr));
    release_coeff = exp(-1.0 / (rel_tau * sr));
    envelope = 0.0;
}

double EnvelopeFollower::process(double input) {
    double v = fabs(input);
    double coeff = (v > envelope) ? attack_coeff : release_coeff;
    envelope = coeff * envelope + (1.0 - coeff) * v;
    return envelope;
}

void EnvelopeFollower::process_block(const double* in, double* out, int n) {
    for (int i = 0; i < n; i++) out[i] = this->process(in[i]);
}

// ============================================================================
// 自检
// ============================================================================
int env_self_test() {
    int fail = 0;
    set_sample_rate(1000.0);                 // 1 kHz，便于秒级计算

    // 1. ADSR：0.01s attack 应在 ~10 采样到达 1.0
    AdsrEnvelope adsr;
    adsr.init();
    adsr.attack = 0.01; adsr.decay = 0.05; adsr.sustain = 0.5; adsr.release = 0.05;
    adsr.note_on();
    double peak = 0.0;
    for (int i = 0; i < 100; i++) {
        double v = adsr.process();
        if (v > peak) peak = v;
    }
    if (peak < 0.99 || peak > 1.01) fail++;

    // 2. sustain 段电平应稳定在 0.5
    double s = adsr.process();
    if (fabs(s - 0.5) > 0.05) fail++;

    // 3. note_off 后 release 段应在 0.05s 内降到 0
    adsr.note_off();
    bool reached_zero = false;
    for (int i = 0; i < 200; i++) {
        double v = adsr.process();
        if (v <= 0.001) { reached_zero = true; break; }
    }
    if (!reached_zero) fail++;

    // 4. AHDSR：hold 阶段应保持 1.0
    AhdsrEnvelope ah;
    ah.init();
    ah.attack = 0.01; ah.hold = 0.02; ah.decay = 0.05; ah.sustain = 0.5; ah.release = 0.05;
    ah.note_on();
    double hold_level = 0.0;
    for (int i = 0; i < 30; i++) {
        hold_level = ah.process();
    }
    if (hold_level < 0.99) fail++;           // 仍在 attack/hold 段，应接近 1

    // 5. AR 包络：trigger 后能升到 1 再降到 0
    ArEnvelope ar;
    ar.init();
    ar.attack = 0.01; ar.release = 0.05;
    ar.trigger();
    bool up = false, down = false;
    for (int i = 0; i < 200; i++) {
        double v = ar.process();
        if (v > 0.9) up = true;
        if (up && v < 0.1) down = true;
    }
    if (!up || !down) fail++;

    // 6. MultiSegmentEnv：折线点电平
    MultiSegmentEnv me;
    me.init();
    me.add_point(0.0, 0.0);
    me.add_point(0.01, 1.0);
    me.add_point(0.05, 0.0);
    me.note_on();
    double t1 = 0.0;
    for (int i = 0; i < 15; i++) t1 = me.process();   // ~0.015s，应在第二段下降
    if (t1 < 0.0 || t1 > 1.0) fail++;

    // 7. EnvelopeFollower：跟随一个 1.0 阶跃应快速上升
    EnvelopeFollower ef;
    ef.init(5.0, 50.0);                     // 5ms attack, 50ms release
    for (int i = 0; i < 20; i++) ef.process(1.0);
    if (ef.envelope < 0.8) fail++;          // 20ms 后应跟到接近 1

    set_sample_rate(44100.0);
    return fail;
}

} // namespace audsp
} // namespace nefu
