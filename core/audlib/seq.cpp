// nefuOS audlib —— 音序器与音符实现 + 自测
#include "audlib/seq.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace nefu {
namespace audx {

double Note::midi_to_freq(int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

int Note::name_to_midi(const std::string& name) {
    if (name.size() < 2) return -1;
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int semitone = -1;
    // 支持 C, C#, Db 等写法：简单匹配前 1~2 字符
    for (int i = 0; i < 12; i++) {
        const char* n = names[i];
        if (name[0] == n[0]) {
            if (n[1] == 0 || (name.size() > 1 && name[1] == n[1])) { semitone = i; break; }
        }
    }
    if (semitone < 0) return -1;
    int octave = name.back() - '0';
    if (octave < 0 || octave > 9) return -1;
    // C4 = 60；octave 4 时 semitone 0
    return (octave + 1) * 12 + semitone;
}

int Note::freq_to_midi(double freq) {
    if (freq <= 0) return -1;
    return (int)std::round(69 + 12 * std::log2(freq / 440.0));
}

std::string Note::midi_to_name(int midi) {
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = midi / 12 - 1;
    int s = midi % 12;
    char buf[8];
    snprintf(buf, sizeof(buf), "%s%d", names[s], octave);
    return std::string(buf);
}

// ---- self test ----
int Note::self_test() {
    int fails = 0;
    // 1. A4 = 440
    if (std::abs(Note::midi_to_freq(69) - 440) > 1e-6) fails++;
    // 2. C4 = 60 = 约 261.6
    {
        double f = Note::midi_to_freq(60);
        if (std::abs(f - 261.625565) > 0.01) fails++;
    }
    // 3. 音名转换
    {
        if (Note::name_to_midi("C4") != 60) fails++;
        if (Note::name_to_midi("A4") != 69) fails++;
        if (Note::name_to_midi("C5") != 72) fails++;
        if (Note::name_to_midi("ZZ") != -1) fails++;
    }
    // 4. 频率反向
    {
        if (Note::freq_to_midi(440.0) != 69) fails++;
        if (Note::freq_to_midi(261.625565) != 60) fails++;
    }
    // 5. 名称往返
    {
        if (Note::midi_to_name(60) != "C4") fails++;
        if (Note::midi_to_name(69) != "A4") fails++;
        if (Note::midi_to_name(72) != "C5") fails++;
    }
    return fails;
}

Sequencer::Sequencer(int sample_rate, int bpm) : rate(sample_rate), tempo(bpm) {
    for (int i = 0; i < 16; i++) { midis[i] = -1; gates[i] = false; }
}

void Sequencer::set_step(int step, int midi, bool gate) {
    if (step < 0 || step >= 16) return;
    midis[step] = midi;
    gates[step] = gate;
}

AudioBuf Sequencer::play_loop(int wave_type, double amp) const {
    double step_sec = 60.0 / tempo / 4.0;   // 四分音符步长
    int step_frames = (int)(rate * step_sec);
    int total = step_frames * 16;
    AudioBuf a(rate, 1);
    a.silence(total);
    for (int st = 0; st < 16; st++) {
        if (midis[st] < 0 || !gates[st]) continue;
        double freq = Note::midi_to_freq(midis[st]);
        // 生成该步的短音（含包络：简化用 90% 步长并淡出）
        Synth s(rate);
        AudioBuf t = s.tone(step_sec * 0.9, wave_type, freq, amp);
        // 淡出防爆音
        t.fade_out((int)(step_frames * 0.1));
        int n = t.frames();
        if (n > step_frames) n = step_frames;
        for (int i = 0; i < n; i++) a.data[st * step_frames + i] += t.data[i];
    }
    return a;
}

AudioBuf Sequencer::play_bars(int bars, int wave_type, double amp) const {
    AudioBuf one = play_loop(wave_type, amp);
    AudioBuf a(rate, 1);
    a.silence(one.frames() * bars);
    for (int b = 0; b < bars; b++)
        for (int i = 0; i < one.frames(); i++)
            a.data[b * one.frames() + i] = one.data[i];
    return a;
}

// ---- self test ----
int Sequencer::self_test() {
    int fails = 0;
    Sequencer sq(8000, 120);
    // 1. 空音序器输出静音
    {
        AudioBuf a = sq.play_loop(WAVE_SINE, 0.5);
        if (a.frames() != (int)(8000 * 60.0 / 120 / 4.0) * 16) fails++;
        bool silent = true;
        for (int i = 0; i < a.frames(); i++)
            if (a.data[i] != 0) silent = false;
        if (!silent) fails++;
    }
    // 2. 填一个音符有声
    {
        Sequencer s2(8000, 120);
        s2.set_step(0, 60, true);   // C4
        AudioBuf a = s2.play_loop(WAVE_SINE, 0.5);
        bool has_sound = false;
        for (int i = 0; i < a.frames(); i++)
            if (a.data[i] != 0) has_sound = true;
        if (!has_sound) fails++;
    }
    // 3. gate=false 无声音
    {
        Sequencer s3(8000, 120);
        s3.set_step(0, 60, false);
        AudioBuf a = s3.play_loop(WAVE_SINE, 0.5);
        bool silent = true;
        for (int i = 0; i < a.frames(); i++)
            if (a.data[i] != 0) silent = false;
        if (!silent) fails++;
    }
    // 4. 多小节长度
    {
        Sequencer s4(8000, 120);
        s4.set_step(0, 60, true);
        AudioBuf a = s4.play_bars(2, WAVE_SINE, 0.3);
        AudioBuf one = s4.play_loop(WAVE_SINE, 0.3);
        if (a.frames() != one.frames() * 2) fails++;
    }
    return fails;
}

} // namespace audx
} // namespace nefu
