// audio_engine.cpp —— 音频引擎实现：合成器 + 混音 + 自测
#include "audio_engine.h"

#include "../lib/softmath.h"
#include <stddef.h>

namespace nefu {
namespace gameengine {

// ============================================================================
//  AudioVoice 取一帧
// ============================================================================
bool AudioVoice::read_sample(int16_t& out_left, int16_t& out_right, int step_ms) {
    if (!playing || !sample || !sample->data) {
        out_left = out_right = 0;
        return false;
    }
    if (position >= sample->length) {
        if (loop) position = 0;
        else { playing = false; out_left = out_right = 0; return false; }
    }

    // 淡入淡出：按 step_ms 调整音量
    if (fade_ms > 0) {
        fade_ms -= step_ms;
        if (fade_ms < 0) fade_ms = 0;
        // 线性插值当前音量 -> 目标
        fix cur = nefu::fx::itofix(volume);
        fix tgt = nefu::fx::itofix(fade_target_vol);
        fix ratio = nefu::fx::fx_div(nefu::fx::itofix(fade_total - fade_ms),
                                     nefu::fx::itofix(fade_total));
        fix v = cur + nefu::fx::fx_mul(tgt - cur, ratio);
        volume = nefu::fx::fixtoi(v);
        if (fade_ms == 0) volume = fade_target_vol;
    }

    int16_t s = sample->data[position];
    position++;

    // 音量缩放（0..255 -> 0..1.0）
    int scaled = (int)s * volume / 255;

    // 声像：pan -128..127
    int lv = scaled, rv = scaled;
    if (pan < 0)      rv = (int)scaled * (128 + pan) / 128;
    else if (pan > 0) lv = (int)scaled * (128 - pan) / 128;

    out_left = (int16_t)lv;
    out_right = (int16_t)rv;
    return true;
}

// ============================================================================
//  Synthesizer
// ============================================================================
void Synthesizer::tone(AudioSample& out, int freq_hz, int dur_ms,
                       WaveForm wave, int vol) {
    int n = out.rate * dur_ms / 1000;
    if (n < 1) n = 1;
    out.alloc(n, out.rate);

    // 相位增量（每采样）：2*pi*freq / rate，用定点
    fix phase_inc = nefu::fx::fx_div(nefu::fx::fx_mul(nefu::fx::FX_2PI,
                                      nefu::fx::itofix(freq_hz)),
                                      nefu::fx::itofix(out.rate));
    fix phase = 0;
    int attack = n / 10;     // 10% 起音
    int release = n / 5;     // 20% 释放

    for (int i = 0; i < n; i++) {
        int16_t v = 0;
        switch (wave) {
        case WAVE_SINE:
            v = (int)(nefu::fx::fx_sin(phase) >> 8);   // Q16.16 -> 近似 16-bit
            break;
        case WAVE_SQUARE:
            v = (phase < nefu::fx::FX_PI) ? 12000 : -12000;
            break;
        case WAVE_SAWTOOTH: {
            // 相位 0..2pi -> -1..1
            fix norm = nefu::fx::fx_div(phase, nefu::fx::FX_2PI);   // 0..1
            v = (int)(nefu::fx::fx_mul(norm, nefu::fx::itofix(24000)) - nefu::fx::itofix(12000));
            break;
        }
        case WAVE_TRIANGLE: {
            fix norm = nefu::fx::fx_div(phase, nefu::fx::FX_2PI);   // 0..1
            // 0..0.5 升，0.5..1 降
            fix tri = (norm < nefu::fx::FX_HALF)
                      ? nefu::fx::fx_mul(norm, nefu::fx::itofix(24000))
                      : nefu::fx::fx_mul(nefu::fx::FX_ONE - norm, nefu::fx::itofix(24000));
            v = (int)(tri - nefu::fx::itofix(12000));
            break;
        }
        case WAVE_NOISE:
            v = (int16_t)((i * 1103515245 + 12345) & 0x7FFF) - 16384;
            break;
        }
        // ADSR：起音 + 释放包络
        int env = 255;
        if (i < attack) env = i * 255 / (attack > 0 ? attack : 1);
        else if (i > n - release) env = (n - i) * 255 / (release > 0 ? release : 1);
        v = v * env * vol / (255 * 255);
        out.data[i] = (int16_t)v;
        phase += phase_inc;
        if (phase >= nefu::fx::FX_2PI) phase -= nefu::fx::FX_2PI;
    }
}

void Synthesizer::noise(AudioSample& out, int dur_ms, int vol, uint32_t seed) {
    int n = out.rate * dur_ms / 1000;
    if (n < 1) n = 1;
    out.alloc(n, out.rate);
    uint32_t s = seed ? seed : 1;
    for (int i = 0; i < n; i++) {
        s = s * 1103515245u + 12345u;
        int16_t v = (int16_t)((s >> 16) & 0x7FFF);
        // 衰减包络
        int env = (n - i) * 255 / n;
        out.data[i] = (int16_t)(v * env * vol / (32767 * 255));
    }
}

void Synthesizer::sweep(AudioSample& out, int from_hz, int to_hz, int dur_ms,
                        WaveForm wave, int vol) {
    int n = out.rate * dur_ms / 1000;
    if (n < 1) n = 1;
    out.alloc(n, out.rate);
    fix phase = 0;
    for (int i = 0; i < n; i++) {
        fix t = nefu::fx::fx_div(nefu::fx::itofix(i), nefu::fx::itofix(n));
        int freq = from_hz + (to_hz - from_hz) * i / n;
        fix phase_inc = nefu::fx::fx_div(nefu::fx::fx_mul(nefu::fx::FX_2PI,
                                          nefu::fx::itofix(freq)),
                                          nefu::fx::itofix(out.rate));
        int16_t v = (int16_t)(nefu::fx::fx_sin(phase) >> 8);
        (void)wave;
        int env = (n - i) * 255 / n;
        out.data[i] = (int16_t)(v * env * vol / (32767 * 255));
        phase += phase_inc;
        if (phase >= nefu::fx::FX_2PI) phase -= nefu::fx::FX_2PI;
    }
}

// ============================================================================
//  Mixer
// ============================================================================
int AudioMixer::mix(int16_t* out, int frames, int dt_ms) {
    for (int i = 0; i < frames; i++) {
        int acc_l = 0, acc_r = 0;
        for (int v = 0; v < GE_MAX_VOICES; v++) {
            int16_t l, r;
            if (voices[v].read_sample(l, r, dt_ms)) {
                acc_l += l;
                acc_r += r;
            }
        }
        // 主音量
        acc_l = acc_l * master_volume / 255;
        acc_r = acc_r * master_volume / 255;
        // 限幅
        if (acc_l > 32767) acc_l = 32767;
        if (acc_l < -32768) acc_l = -32768;
        if (acc_r > 32767) acc_r = 32767;
        if (acc_r < -32768) acc_r = -32768;
        out[i * 2 + 0] = (int16_t)acc_l;
        out[i * 2 + 1] = (int16_t)acc_r;
    }
    return frames;
}

// ============================================================================
//  自测
// ============================================================================
int audio_engine_self_test() {
    int fails = 0;

    // 合成一个 440Hz 正弦，0.1 秒，22050Hz
    AudioSample s;
    s.rate = 22050;
    Synthesizer::tone(s, 440, 100, WAVE_SINE, 200);
    int expected = 22050 * 100 / 1000;   // 2205
    if (s.length != expected) fails++;
    if (!s.data) fails++;

    // 不应全是静音（正弦波有非零采样）
    int nonzero = 0;
    for (int i = 0; i < s.length; i++) if (s.data[i] != 0) nonzero++;
    if (nonzero < s.length / 2) fails++;   // 至少一半非零

    // 噪声样本
    AudioSample n;
    n.rate = 22050;
    Synthesizer::noise(n, 50, 200, 12345);
    if (n.length != 1102) fails++;   // 22050*50/1000 = 1102.5 -> 1102

    // 滑音
    AudioSample sw;
    sw.rate = 22050;
    Synthesizer::sweep(sw, 200, 800, 100, WAVE_SINE, 200);
    if (sw.length != expected) fails++;

    // 混音器：播放一个采样，取几帧
    AudioMixer mx;
    int voice = mx.play(&s, 200, false);
    if (voice < 0) fails++;
    int16_t buf[64];
    mx.mix(buf, 32, 16);
    // 应输出非零
    bool any = false;
    for (int i = 0; i < 64; i++) if (buf[i] != 0) any = true;
    if (!any) fails++;

    // 播完后 voice 停止
    // s 长 2205 采样，已取 32，再取 2205 帧应播完
    int16_t big[512];
    int played = 0;
    while (mx.voices[voice].playing && played < 5000) {
        mx.mix(big, 256, 16);
        played += 256;
    }
    if (mx.voices[voice].playing) fails++;

    // 循环播放不播完
    AudioMixer mx2;
    int v2 = mx2.play(&n, 200, true);   // n 长 1102，循环
    mx2.mix(big, 256, 16);
    mx2.mix(big, 256, 16);
    mx2.mix(big, 256, 16);   // 已取 768 < 1102
    if (!mx2.voices[v2].playing) fails++;
    mx2.stop(v2);
    if (mx2.voices[v2].playing) fails++;


    // --- 音序器 ---
    MusicSequencer seq;
    seq.bpm = 120;
    seq.start();
    // pattern: C E G E（MIDI 60,64,67,64）
    int pat[4] = { 60, 64, 67, 64 };
    int n0 = seq.update(0, pat, 4);
    if (n0 != 60) fails++;
    // 4ms 后不到一个 step（step_ms=125），不触发
    int n1 = seq.update(4, pat, 4);
    if (n1 != -1) fails++;
    // 125ms 后触发下一个
    int n2 = seq.update(125, pat, 4);
    if (n2 != 64) fails++;
    int n3 = seq.update(250, pat, 4);
    if (n3 != 67) fails++;
    // 音符频率表
    if (MusicSequencer::note_freq(60) < 255 || MusicSequencer::note_freq(60) > 270) fails++;
    if (MusicSequencer::note_freq(69) < 435 || MusicSequencer::note_freq(69) > 445) fails++;

    // --- Delay 回声 ---
    DelayNode dly;
    dly.delay_samples = 50;
    dly.mix = 255;
    dly.feedback = 0;
    // 写入一个非零脉冲
    dly.process(0);
    for (int i = 0; i < 50; i++) dly.process(0);
    dly.process(1000);       // 脉冲写入
    for (int i = 0; i < 49; i++) dly.process(0);
    int16_t echo = dly.process(0);   // 此时应读出 50 样本前的脉冲
    if (echo == 0) fails++;

    // --- LowPass ---
    LowPassFilter lp;
    lp.alpha = 255;   // 直通
    int16_t p1 = lp.process(1000);
    if (p1 < 990 || p1 > 1010) fails++;
    lp.reset();
    lp.alpha = 10;   // 强平滑
    lp.process(0);
    int16_t p2 = lp.process(1000);
    if (p2 > 200) fails++;   // 应被平滑到远小于 1000

    // --- NoiseGenerator ---
    NoiseGenerator ng;
    ng.type = 0;
    int16_t nz = ng.next_white();
    if (nz > 32767 || nz < -32768) fails++;
    // 白噪声均值应接近 0（采样 100 个）
    int32_t sum = 0;
    for (int i = 0; i < 100; i++) sum += ng.next_white();
    if (sum > 100000 || sum < -100000) fails++;
    // 粉噪声
    ng.type = 1;
    int16_t p = ng.next_pink();
    if (p > 32767 || p < -32768) fails++;

    // --- DrumSequencer ---
    DrumSequencer ds;
    ds.pattern[