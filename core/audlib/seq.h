// nefuOS audlib —— 音序器 seq 与音符 note
// 教学版：音符频率表 + 步进音序器（pattern 播放）。
// 用于简单编曲与音乐教学。class / STL / 中文注释。
#pragma once
#include <vector>
#include <string>
#include "audlib/synth.h"

namespace nefu {
namespace audx {

// 音符频率表
class Note {
public:
    // MIDI 音符 -> 频率
    static double midi_to_freq(int midi);
    // 音名 -> MIDI 号（如 "C4" -> 60，"A4" -> 69；失败返回 -1）
    static int name_to_midi(const std::string& name);
    // 频率 -> 最近 MIDI 号
    static int freq_to_midi(double freq);
    // 音名（如 "C4"）
    static std::string midi_to_name(int midi);

    // ---- self test ----
    static int self_test();
};

// 步进音序器：16 步 pattern，每步一个 MIDI 音符 + 门（gate）
class Sequencer {
public:
    // 构造：采样率、BPM
    Sequencer(int sample_rate, int bpm);

    // 设置第 step 步的音符（midi；-1 为休止）
    void set_step(int step, int midi, bool gate);
    // 播放一个 16 步循环，返回合成音频
    // wave_type 波形类型，每步时长由 BPM 决定（四分音符 = 60/BPM 秒）
    AudioBuf play_loop(int wave_type, double amp) const;
    // 播放指定小节数
    AudioBuf play_bars(int bars, int wave_type, double amp) const;

    // 步数 / BPM
    int steps() const { return 16; }
    int bpm() const { return tempo; }
    void set_bpm(int b) { if (b > 0) tempo = b; }

    // ---- self test ----
    static int self_test();

private:
    int rate, tempo;
    int midis[16];
    bool gates[16];
};

} // namespace audx
} // namespace nefu
