// ============================================================================
// nefuOS 音频DSP库 —— MIDI 文件解析模块 (midi.h)
// ----------------------------------------------------------------------------
// 支持 Standard MIDI File (SMF) Format 0 / Format 1。
// 事件类型：
//   Note On / Note Off / Poly Aftertouch / Control Change /
//   Program Change / Channel Aftertouch / Pitch Bend /
//   Meta (tempo / time sig / key sig / track name / end of track) /
//   SysEx
//
// 事件迭代器：调用 midi_load() 后，用 midi_next_event() 顺序读出事件。
// ============================================================================
#pragma once
#include <stdint.h>

namespace nefu {
namespace audsp {

// ---- MIDI 事件类型 ----
enum MidiEventType {
    MEVENT_INVALID = 0,
    MEVENT_NOTE_OFF,         // data1=note, data2=vel
    MEVENT_NOTE_ON,          // data1=note, data2=vel (vel=0 等价 note off)
    MEVENT_POLY_AT,
    MEVENT_CC,               // data1=controller, data2=value
    MEVENT_PROGRAM,          // data1=program
    MEVENT_CHAN_AT,
    MEVENT_PITCHBEND,        // data1/data2 合成 14-bit
    MEVENT_META,             // meta_type, meta_len, meta_data
    MEVENT_SYSEX,
    MEVENT_END_OF_TRACK
};

// ---- 解析出的事件 ----
struct MidiEvent {
    int    type;             // MidiEventType
    int    channel;          // 0..15
    int    delta_ticks;      // 距上一事件的 ticks
    int    abs_ticks;        // 绝对 ticks
    int    data1, data2;
    int    meta_type;
    int    meta_len;
    const uint8_t* meta_data;
};

// ---- SMF 文件容器 ----
struct MidiFile {
    int    format;           // 0 / 1 / 2
    int    ntracks;
    int    division;          // ticks per quarter note
    int    tempo_us_per_qn;  // 微秒/四分音符 (默认 500000 = 120 BPM)
    // 原始数据
    const uint8_t* data;
    int    length;
    // 迭代状态
    int    track_idx;
    int    track_offset;     // 当前 track 在 data 中的偏移
    int    track_end;
    int    running_status;
    int    current_tick;
    bool   loaded;
};

// ---- 加载/解析 ----
// data 必须保持生命周期（本模块不拷贝）。返回 0 成功。
int  midi_load(MidiFile* f, const uint8_t* data, int length);
void midi_close(MidiFile* f);

// ---- 迭代器：读出下一个事件，返回 0 成功，-1 结束 ----
int  midi_next_event(MidiFile* f, MidiEvent* ev);

// ---- 工具 ----
double midi_ticks_to_seconds(int ticks, int division, int tempo_us);
const char* midi_event_type_name(int t);
const char* midi_note_name(int note);          // e.g. "C4"

// ---- 自检：构造一个最小 SMF 字节流，验证解析 ----
int midi_self_test();

} // namespace audsp
} // namespace nefu
