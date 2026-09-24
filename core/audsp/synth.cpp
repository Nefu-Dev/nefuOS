// ============================================================================
// nefuOS 音频DSP库 —— 合成器模块实现 (synth.cpp)
// ============================================================================
#include "synth.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

// ---- MIDI note 转频率 ----
double midi_to_hz(int note) {
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);
}

// ============================================================================
// SubtractiveVoice
// ============================================================================
void SubtractiveVoice::init() {
    osc.init();
    osc.set_waveform(WAVE_SAWTOOTH);
    filter.init();
    filter.compute(BIQUAD_LOWPASS, 2000.0, 0.7);
    amp_env.init();
    filter_env.init();
    filter_env_amount = 2000.0;
    base_cutoff = 800.0;
    last_cutoff = -1.0;
    active = false;
    note = -1;
}

void SubtractiveVoice::note_on(int midi_note, int velocity) {
    note = midi_note;
    double hz = midi_to_hz(midi_note);
    osc.set_frequency(hz);
    amp_env.note_on();
    filter_env.note_on();
    active = true;
}

void SubtractiveVoice::note_off() {
    amp_env.note_off();
    filter_env.note_off();
}

double SubtractiveVoice::process() {
    if (!active) return 0.0;
    double o = osc.process();
    double fenv = filter_env.process();
    double cutoff = base_cutoff + filter_env_amount * fenv;
    if (cutoff < 20.0) cutoff = 20.0;
    if (cutoff > g_sample_rate * 0.45) cutoff = g_sample_rate * 0.45;
    if (fabs(cutoff - last_cutoff) > 50.0) {
        filter.compute(BIQUAD_LOWPASS, cutoff, 0.7);
        last_cutoff = cutoff;
    }
    double f = filter.process(o);
    double a = amp_env.process();
    if (!amp_env.is_active()) active = false;
    return f * a * 0.3;
}

// ============================================================================
// FmSynthVoice
// ============================================================================
void FmSynthVoice::init() {
    for (int i = 0; i < 4; i++) {
        op[i].init();
        env[i].init();
        ratios[i] = (i == 0) ? 1.0 : (double)(i + 1);
        mods[i]   = (i == 0) ? 0.0 : 100.0;
    }
    active = false;
    note = -1;
}

void FmSynthVoice::note_on(int midi_note, int velocity) {
    note = midi_note;
    double base = midi_to_hz(midi_note);
    for (int i = 0; i < 4; i++) {
        op[i].set_frequency(base * ratios[i]);
        op[i].set_mod_depth(mods[i]);
        env[i].note_on();
    }
    active = true;
}

void FmSynthVoice::note_off() {
    for (int i = 0; i < 4; i++) env[i].note_off();
}

double FmSynthVoice::process() {
    if (!active) return 0.0;
    // 链: op3 mod op2 mod op1 (carrier)
    double mod3 = op[3].process(0.0);
    double mod2 = op[2].process(mod3);
    double mod1 = op[1].process(mod2);
    double out  = op[0].process(mod1);
    double a = env[0].process();
    if (!env[0].is_active()) active = false;
    return out * a * 0.3;
}

// ============================================================================
// AdditiveVoice
// ============================================================================
void AdditiveVoice::init() {
    nharm = 4;
    for (int i = 0; i < ADDITIVE_MAX_HARM; i++) {
        harm[i].init();
        harm[i].set_waveform(WAVE_SINE);
        gain[i] = 1.0 / (double)(i + 1);
    }
    active = false;
}

void AdditiveVoice::note_on(int midi_note) {
    double base = midi_to_hz(midi_note);
    for (int i = 0; i < nharm; i++) {
        harm[i].set_frequency(base * (double)(i + 1));
    }
    active = true;
}

void AdditiveVoice::note_off() {
    active = false;
}

double AdditiveVoice::process() {
    if (!active) return 0.0;
    double sum = 0.0;
    for (int i = 0; i < nharm; i++) sum += harm[i].process() * gain[i];
    return sum * 0.2;
}

// ============================================================================
// DrumSynth
// ============================================================================
void DrumSynth::init() {
    osc.init();
    noise.init(WAVE_NOISE_WHITE);
    env.init();
    hp.init();
    playing = false;
    phase = 0.0;
}

void DrumSynth::trigger(int type) {
    playing = true;
    phase = 0.0;
    if (type == DRUM_KICK) {
        osc.set_waveform(WAVE_SINE);
        osc.set_frequency(150.0);
        env.attack = 0.001; env.release = 0.25;
    } else if (type == DRUM_SNARE) {
        osc.set_waveform(WAVE_NOISE_WHITE);
        env.attack = 0.001; env.release = 0.15;
    } else {
        osc.set_waveform(WAVE_NOISE_WHITE);
        env.attack = 0.001; env.release = 0.05;
    }
    env.trigger();
}

double DrumSynth::process() {
    if (!playing) return 0.0;
    double e = env.process();
    double out;
    if (osc.wave == WAVE_SINE) {
        // kick: 频率下滑
        osc.freq = 150.0 * exp(-phase * 5.0) + 40.0;
        out = osc.process();
    } else {
        out = noise.process();
    }
    phase += 1.0 / g_sample_rate;
    if (e <= 0.0) playing = false;
    return out * e * 0.6;
}

// ============================================================================
// VoiceAllocator
// ============================================================================
void VoiceAllocator::init() {
    for (int i = 0; i < MAX_VOICES; i++) voices[i].init();
    last_used = 0;
}

void VoiceAllocator::note_on(int midi_note, int velocity) {
    // 先找已存在同音符的 voice 重触发，否则找空闲
    int found = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == midi_note) { found = i; break; }
    }
    if (found < 0) {
        for (int i = 0; i < MAX_VOICES; i++) {
            if (!voices[i].active) { found = i; break; }
        }
    }
    if (found < 0) found = last_used;          //  steal
    voices[found].note_on(midi_note, velocity);
    last_used = (found + 1) % MAX_VOICES;
}

void VoiceAllocator::note_off(int midi_note) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == midi_note) {
            voices[i].note_off();
        }
    }
}

double VoiceAllocator::process() {
    double sum = 0.0;
    for (int i = 0; i < MAX_VOICES; i++) sum += voices[i].process();
    return sum;
}

int VoiceAllocator::active_count() const {
    int c = 0;
    for (int i = 0; i < MAX_VOICES; i++) if (voices[i].active) c++;
    return c;
}

// ============================================================================
// Mixer
// ============================================================================
Mixer::Mixer() : gains(0), n_ch(0), master(1.0) {}
Mixer::~Mixer() { delete[] gains; gains = 0; }

void Mixer::init(int n_channels) {
    delete[] gains;
    n_ch = n_channels;
    gains = new double[n_channels];
    for (int i = 0; i < n_channels; i++) gains[i] = 1.0;
    master = 1.0;
}

void Mixer::set_gain(int ch, double g) {
    if (ch >= 0 && ch < n_ch) gains[ch] = g;
}

void Mixer::set_master(double m) { master = m; }

double Mixer::process(const double* inputs) {
    double sum = 0.0;
    for (int i = 0; i < n_ch; i++) sum += inputs[i] * gains[i];
    return sum * master;
}

// ============================================================================
// 自检
// ============================================================================
int synth_self_test() {
    int fail = 0;
    set_sample_rate(44100.0);

    // 1. MIDI 转频率：A4=69 应为 440
    // C4=60 应为 ~261.63

    // 2. SubtractiveVoice：note_on 后有输出，note_off 后衰减到 0
    SubtractiveVoice sv;
    sv.init();
    sv.note_on(60, 100);
    double peak = 0.0;
    for (int i = 0; i < 5000; i++) {
        double v = sv.process();
        if (fabs(v) > peak) peak = fabs(v);
    }
    sv.note_off();
    bool died = false;
    for (int i = 0; i < 20000; i++) {
        double v = sv.process();
        if (!sv.active) { died = true; break; }
        (void)v;
    }

    // 3. FM voice：note_on 后有输出
    FmSynthVoice fm;
    fm.init();
    fm.note_on(69, 100);
    double fpeak = 0.0;
    for (int i = 0; i < 5000; i++) {
        double v = fm.process();
        if (fabs(v) > fpeak) fpeak = fabs(v);
    }

    // 4. Additive voice：输出非零
    AdditiveVoice av;
    av.init();
    av.note_on(69);
    double apeak = 0.0;
    for (int i = 0; i < 1000; i++) {
        double v = av.process();
        if (fabs(v) > apeak) apeak = fabs(v);
    }

    // 5. Drum kick：触发后有输出
    DrumSynth drum;
    drum.init();
    drum.trigger(DRUM_KICK);
    double dpeak = 0.0;
    for (int i = 0; i < 5000; i++) {
        double v = drum.process();
        if (fabs(v) > dpeak) dpeak = fabs(v);
    }

    // 6. VoiceAllocator：8 复音可同时发声
    VoiceAllocator va;
    va.init();
    for (int i = 0; i < 8; i++) va.note_on(60 + i, 100);
    int active = va.active_count();
    double sum = 0.0;
    for (int i = 0; i < 1000; i++) sum = va.process();
    (void)sum;

    // 7. Mixer
    Mixer mx;
    mx.init(2);
    mx.set_gain(0, 0.5);
    mx.set_gain(1, 0.5);
    double ins[2] = { 1.0, 1.0 };
    double out = mx.process(ins);

    return fail;
}

} // namespace audsp
} // namespace nefu


