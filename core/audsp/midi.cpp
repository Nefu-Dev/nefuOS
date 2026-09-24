// ============================================================================
// nefuOS 音频DSP库 —— MIDI 文件解析实现 (midi.cpp)
// ============================================================================
#include "midi.h"
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace nefu {
namespace audsp {

// ---- 工具：大端读 16/32 位 ----
static uint16_t rd_be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
static uint32_t rd_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | p[3];
}

// ---- 读 variable-length quantity，返回消耗字节数，*out 为值 ----
static int read_vlq(const uint8_t* data, int maxlen, uint32_t* out) {
    uint32_t v = 0;
    int i = 0;
    while (i < maxlen) {
        uint8_t b = data[i];
        v = (v << 7) | (b & 0x7F);
        i++;
        if (!(b & 0x80)) break;
    }
    *out = v;
    return i;
}

double midi_ticks_to_seconds(int ticks, int division, int tempo_us) {
    if (division <= 0) division = 480;
    return (double)ticks * (double)tempo_us / 1000000.0 / (double)division;
}

const char* midi_event_type_name(int t) {
    switch (t) {
    case MEVENT_NOTE_OFF:    return "NoteOff";
    case MEVENT_NOTE_ON:     return "NoteOn";
    case MEVENT_POLY_AT:     return "PolyAT";
    case MEVENT_CC:          return "CC";
    case MEVENT_PROGRAM:     return "Program";
    case MEVENT_CHAN_AT:     return "ChanAT";
    case MEVENT_PITCHBEND:   return "PitchBend";
    case MEVENT_META:        return "Meta";
    case MEVENT_SYSEX:       return "SysEx";
    case MEVENT_END_OF_TRACK:return "EndOfTrack";
    default: return "?";
    }
}

const char* midi_note_name(int note) {
    static const char* names[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    static char buf[8];
    int oct = note / 12 - 1;
    int idx = note % 12;
    // 用静态 buffer 足够 self_test 用
    buf[0] = names[idx][0];
    buf[1] = names[idx][1];
    buf[2] = (char)('0' + oct);
    buf[3] = 0;
    return buf;
}

// ============================================================================
// midi_load
// ============================================================================
int midi_load(MidiFile* f, const uint8_t* data, int length) {
    if (!f || !data || length < 14) return -1;
    memset(f, 0, sizeof(*f));
    f->data = data;
    f->length = length;
    f->tempo_us_per_qn = 500000;

    // 头 chunk: "MThd" + len(4) + format(2) + ntracks(2) + division(2)
    if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
        return -2;
    uint32_t hdr_len = rd_be32(data + 4);
    if (hdr_len < 6) return -3;
    f->format   = rd_be16(data + 8);
    f->ntracks  = rd_be16(data + 10);
    f->division = rd_be16(data + 12);
    if (f->ntracks <= 0) return -4;

    // 定位第一个 track
    int off = 8 + (int)hdr_len;
    // 跳过 track 头
    if (off + 8 > length) return -5;
    if (data[off] != 'M' || data[off+1] != 'T' || data[off+2] != 'r' || data[off+3] != 'k')
        return -6;
    uint32_t tlen = rd_be32(data + off + 4);
    f->track_offset = off + 8;
    f->track_end    = f->track_offset + (int)tlen;
    f->track_idx = 0;
    f->running_status = 0;
    f->current_tick = 0;
    f->loaded = true;
    return 0;
}

void midi_close(MidiFile* f) {
    if (!f) return;
    f->loaded = false;
    f->data = 0;
}

// ============================================================================
// midi_next_event
// ============================================================================
int midi_next_event(MidiFile* f, MidiEvent* ev) {
    if (!f || !f->loaded || !ev) return -1;
    memset(ev, 0, sizeof(*ev));

    const uint8_t* d = f->data;
    int pos = f->track_offset;
    int end = f->track_end;
    if (pos >= end) return -1;

    // 1. delta time
    uint32_t delta = 0;
    int n = read_vlq(d + pos, end - pos, &delta);
    pos += n;
    f->current_tick += delta;
    ev->delta_ticks = (int)delta;
    ev->abs_ticks = f->current_tick;

    // 2. status byte
    uint8_t status;
    if (pos >= end) return -1;
    uint8_t b0 = d[pos];
    if (b0 & 0x80) {
        status = b0;
        f->running_status = b0;
        pos++;
    } else {
        // running status
        status = f->running_status;
    }

    ev->channel = status & 0x0F;
    uint8_t hi = status & 0xF0;

    switch (hi) {
    case 0x80:   // Note Off
        ev->type = MEVENT_NOTE_OFF;
        ev->data1 = d[pos++];
        ev->data2 = d[pos++];
        break;
    case 0x90:   // Note On
        ev->type = MEVENT_NOTE_ON;
        ev->data1 = d[pos++];
        ev->data2 = d[pos++];
        if (ev->data2 == 0) ev->type = MEVENT_NOTE_OFF;
        break;
    case 0xA0:
        ev->type = MEVENT_POLY_AT;
        ev->data1 = d[pos++];
        ev->data2 = d[pos++];
        break;
    case 0xB0:
        ev->type = MEVENT_CC;
        ev->data1 = d[pos++];
        ev->data2 = d[pos++];
        break;
    case 0xC0:
        ev->type = MEVENT_PROGRAM;
        ev->data1 = d[pos++];
        break;
    case 0xD0:
        ev->type = MEVENT_CHAN_AT;
        ev->data1 = d[pos++];
        break;
    case 0xE0: {
        ev->type = MEVENT_PITCHBEND;
        uint8_t l = d[pos++];
        uint8_t h = d[pos++];
        ev->data1 = l;
        ev->data2 = h;
        break;
    }
    case 0xF0: {
        if (status == 0xFF) {
            // Meta event
            ev->type = MEVENT_META;
            ev->meta_type = d[pos++];
            uint32_t mlen = 0;
            pos += read_vlq(d + pos, end - pos, &mlen);
            ev->meta_len = (int)mlen;
            ev->meta_data = d + pos;
            if (ev->meta_type == 0x2F) ev->type = MEVENT_END_OF_TRACK;
            if (ev->meta_type == 0x51 && mlen >= 3) {
                // tempo: 24-bit us/qn
                f->tempo_us_per_qn = (d[pos] << 16) | (d[pos+1] << 8) | d[pos+2];
            }
            pos += mlen;
        } else if (status == 0xF0 || status == 0xF7) {
            ev->type = MEVENT_SYSEX;
            uint32_t slen = 0;
            pos += read_vlq(d + pos, end - pos, &slen);
            ev->meta_len = (int)slen;
            ev->meta_data = d + pos;
            pos += slen;
        } else {
            return -2;
        }
        break;
    }
    default:
        return -3;
    }

    f->track_offset = pos;
    return 0;
}

// ============================================================================
// 自检
// ============================================================================
int midi_self_test() {
    int fail = 0;

    // 构造一个最小 SMF:
    //   MThd (6 字节: format=0, ntracks=1, division=480)
    //   MTrk:  delta=0, NoteOn(ch0, note=60, vel=100)
    //          delta=120, NoteOff(ch0, note=60, vel=0)
    //          delta=0, Meta EndOfTrack
    uint8_t smf[128];
    int p = 0;
    // Header
    smf[p++] = 'M'; smf[p++] = 'T'; smf[p++] = 'h'; smf[p++] = 'd';
    smf[p++] = 0; smf[p++] = 0; smf[p++] = 0; smf[p++] = 6;   // length=6
    smf[p++] = 0; smf[p++] = 0;                              // format 0
    smf[p++] = 0; smf[p++] = 1;                              // 1 track
    smf[p++] = 0; smf[p++] = 0x01;                           // division=256
    // Track chunk
    int track_len_pos = p + 4;
    smf[p++] = 'M'; smf[p++] = 'T'; smf[p++] = 'r'; smf[p++] = 'k';
    smf[p++] = 0; smf[p++] = 0; smf[p++] = 0; smf[p++] = 0;  // 长度后填
    int track_start = p;
    // delta=0, NoteOn 90 3C 64
    smf[p++] = 0x00;
    smf[p++] = 0x90; smf[p++] = 0x3C; smf[p++] = 0x64;
    // delta=120 (0x78), NoteOff 80 3C 40
    smf[p++] = 0x78;
    smf[p++] = 0x80; smf[p++] = 0x3C; smf[p++] = 0x40;
    // delta=0, Meta FF 2F 00
    smf[p++] = 0x00;
    smf[p++] = 0xFF; smf[p++] = 0x2F; smf[p++] = 0x00;
    int track_len = p - track_start;
    smf[track_len_pos]     = (track_len >> 24) & 0xFF;
    smf[track_len_pos + 1] = (track_len >> 16) & 0xFF;
    smf[track_len_pos + 2] = (track_len >> 8) & 0xFF;
    smf[track_len_pos + 3] = track_len & 0xFF;

    MidiFile mf;
    int rc = midi_load(&mf, smf, p);
    if (rc != 0) { fail++; return fail; }
    if (mf.format != 0) fail++;
    if (mf.ntracks != 1) fail++;

    // 读事件
    MidiEvent ev;
    int got_noteon = 0, got_noteoff = 0, got_end = 0;
    while (midi_next_event(&mf, &ev) == 0) {
        if (ev.type == MEVENT_NOTE_ON && ev.data1 == 60) got_noteon++;
        if (ev.type == MEVENT_NOTE_OFF && ev.data1 == 60) got_noteoff++;
        if (ev.type == MEVENT_END_OF_TRACK) got_end++;
    }
    if (got_noteon != 1) fail++;
    if (got_noteoff != 1) fail++;
    if (got_end != 1) fail++;

    // midi_ticks_to_seconds: 480 ticks, 500000us/qn -> 0.5s
    double s = midi_ticks_to_seconds(480, 480, 500000);
    if (fabs(s - 0.5) > 0.01) fail++;

    midi_close(&mf);
    return fail;
}

} // namespace audsp
} // namespace nefu
