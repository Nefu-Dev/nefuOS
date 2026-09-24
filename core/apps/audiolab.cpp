// nefuOS AudioLab —— 音频DSP 实验室窗口应用
// 参考 algoviz.cpp 的 create_window + userdata + on_paint/on_key/on_close 模式。
// 功能：波形选择 + 频率 + ADSR + 低通滤波，按 G 生成波形并写 WAV 到 /tmp。
#include "apps.h"
#include "../gui/wm.h"
#include "../gui/gfx.h"
#include "../platform.h"
#include "../vfs/vfs.h"
#include "../audsp/audsp_all.h"
#include "../klib/klib.h"

namespace nefu {

namespace {

const int LAB_W = 640, LAB_H = 420;

// AudioLab 状态
struct LabState {
    int    wave;          // Waveform
    double freq;          // Hz
    double attack, decay, sustain, release;  // ADSR 秒
    double cutoff;        // 低通截止 Hz
    double* samples;      // 生成的波形缓冲 [-1,1]
    int     nsamples;
    bool    generated;

    LabState() {
        wave = WAVE_SINE;
        freq = 440.0;
        attack = 0.01; decay = 0.1; sustain = 0.7; release = 0.2;
        cutoff = 4000.0;
        samples = 0;
        nsamples = 0;
        generated = false;
    }

    // 生成 1 秒 @44100 的波形：osc -> adsr -> lowpass
    void generate() {
        if (!samples) {
            nsamples = 44100;
            samples = new double[nsamples];
        }
        audsp::set_sample_rate(44100.0);
        audsp::Oscillator osc;
        osc.init();
        osc.set_waveform(wave);
        osc.set_frequency(freq);
        audsp::AdsrEnvelope env;
        env.init();
        env.attack = attack; env.decay = decay;
        env.sustain = sustain; env.release = release;
        audsp::Biquad lp;
        lp.init();
        lp.compute(audsp::BIQUAD_LOWPASS, cutoff, 0.7);
        env.note_on();
        // 前 0.6s 按键按住，后 0.4s 释放
        int hold = (int)(0.6 * 44100.0);
        for (int i = 0; i < nsamples; i++) {
            if (i == hold) env.note_off();
            double o = osc.process();
            double e = env.process();
            double f = lp.process(o);
            samples[i] = f * e * 0.6;
        }
        generated = true;
    }

    // 写 16-bit PCM WAV 到 VFS /tmp/audiolab.wav
    void write_wav() {
        if (!generated) generate();
        int bytes = 44 + nsamples * 2;
        uint8_t* buf = new uint8_t[bytes];
        // RIFF 头
        buf[0]='R'; buf[1]='I'; buf[2]='F'; buf[3]='F';
        uint32_t sz = (uint32_t)(bytes - 8);
        buf[4]=sz; buf[5]=sz>>8; buf[6]=sz>>16; buf[7]=sz>>24;
        buf[8]='W'; buf[9]='A'; buf[10]='V'; buf[11]='E';
        // fmt chunk
        buf[12]='f'; buf[13]='m'; buf[14]='t'; buf[15]=' ';
        buf[16]=16; buf[17]=0; buf[18]=0; buf[19]=0;       // chunk size 16
        buf[20]=1; buf[21]=0;                              // PCM
        buf[22]=1; buf[23]=0;                              // mono
        uint32_t sr = 44100;
        buf[24]=sr; buf[25]=sr>>8; buf[26]=sr>>16; buf[27]=sr>>24;
        uint32_t br = 44100 * 2;                           // byte rate
        buf[28]=br; buf[29]=br>>8; buf[30]=br>>16; buf[31]=br>>24;
        buf[32]=2; buf[33]=0;                              // block align
        buf[34]=16; buf[35]=0;                             // bits per sample
        // data chunk
        buf[36]='d'; buf[37]='a'; buf[38]='t'; buf[39]='a';
        uint32_t ds = (uint32_t)(nsamples * 2);
        buf[40]=ds; buf[41]=ds>>8; buf[42]=ds>>16; buf[43]=ds>>24;
        for (int i = 0; i < nsamples; i++) {
            double v = samples[i];
            if (v > 1.0) v = 1.0; if (v < -1.0) v = -1.0;
            int16_t s = (int16_t)(v * 32767.0);
            buf[44 + i*2]     = s & 0xFF;
            buf[44 + i*2 + 1] = (s >> 8) & 0xFF;
        }
        FSNode* f = g_vfs->resolve("/tmp/audiolab.wav");
        if (!f) f = g_vfs->create_file("/tmp/audiolab.wav");
        if (f) g_vfs->write_file(f, buf, bytes);
        delete[] buf;
    }

    ~LabState() { delete[] samples; samples = 0; }
};

static LabState* lab_of(Window* w) { return (LabState*)w->userdata; }

static void lab_paint(Window* w) {
    LabState* s = lab_of(w);
    Surface& surf = w->back;
    // 背景
    gfx::fillrect(surf, 0, 0, LAB_W, LAB_H, 0x00FAF8EF);
    gfx::text_scale(surf, 10, 8, "AudioLab", 0x00776756, 0x00FAF8EF, 2);

    char buf[128];
    ksprintf(buf, sizeof(buf), "Wave: %s   Freq: %.0f Hz",
             audsp::waveform_name(s->wave), s->freq);
    gfx::text(surf, 10, 40, buf, 0x00333333, 0x00FAF8EF);
    ksprintf(buf, sizeof(buf), "ADSR: %.0f/%.0f/%d%%/%.0f ms   Cutoff: %.0f Hz",
             s->attack*1000, s->decay*1000, (int)(s->sustain*100),
             s->release*1000, s->cutoff);
    gfx::text(surf, 10, 56, buf, 0x00555555, 0x00FAF8EF);

    // 波形绘制区
    int ox = 20, oy = 90, ow = LAB_W - 40, oh = LAB_H - 160;
    gfx::fillrect(surf, ox, oy, ow, oh, 0x00222222);
    // 中线
    gfx::fillrect(surf, ox, oy + oh/2, ow, 1, 0x00444444);

    if (s->generated && s->samples) {
        // 抽样绘制：每个 x 列取该列内最大绝对值
        for (int x = 0; x < ow; x++) {
            int i0 = (int)((double)x / ow * s->nsamples);
            int i1 = (int)((double)(x+1) / ow * s->nsamples);
            if (i1 <= i0) i1 = i0 + 1;
            double mx = 0.0;
            for (int i = i0; i < i1 && i < s->nsamples; i++) {
                double a = s->samples[i] > 0 ? s->samples[i] : -s->samples[i];
                if (a > mx) mx = a;
            }
            int h = (int)(mx * (oh/2 - 4));
            gfx::fillrect(surf, ox + x, oy + oh/2 - h, 1, h*2, 0x0000E0E0);
        }
    } else {
        gfx::text(surf, ox + 10, oy + oh/2 - 6, "Press G to generate waveform",
                  0x00888888, 0x00222222);
    }

    gfx::text(surf, 10, LAB_H - 50,
              "1-6: wave  Up/Dn: freq  A/D: atk  S/R: sus/rel  C: cutoff  G: gen+WAV",
              0x00909090, 0x00FAF8EF);
    gfx::text(surf, 10, LAB_H - 30,
              "WAV written to /tmp/audiolab.wav (44.1k 16-bit mono)   Esc: close",
              0x00909090, 0x00FAF8EF);
}

static void lab_key(Window* w, const KeyEvent* e) {
    if (!e->down) return;
    LabState* s = lab_of(w);
    switch (e->ascii) {
    case '1': s->wave = audsp::WAVE_SINE; break;
    case '2': s->wave = audsp::WAVE_SQUARE; break;
    case '3': s->wave = audsp::WAVE_SAWTOOTH; break;
    case '4': s->wave = audsp::WAVE_TRIANGLE; break;
    case '5': s->wave = audsp::WAVE_PULSE; break;
    case '6': s->wave = audsp::WAVE_NOISE_WHITE; break;
    case 'a': case 'A': s->attack += 0.005; break;
    case 'd': case 'D': s->decay += 0.02; break;
    case 's': case 'S': s->sustain += 0.1; if (s->sustain > 1) s->sustain = 1; break;
    case 'r': case 'R': s->release += 0.05; break;
    case 'c': case 'C': s->cutoff += 500.0; if (s->cutoff > 16000) s->cutoff = 16000; break;
    case 'g': case 'G': s->generate(); s->write_wav(); break;
    }
    if (e->keycode == KEY_UP)   { s->freq *= 1.1; if (s->freq > 4000) s->freq = 4000; }
    if (e->keycode == KEY_DOWN) { s->freq /= 1.1; if (s->freq < 20) s->freq = 20; }
    if (e->keycode == KEY_ESC) g_wm->close_window(w);
    w->invalidate();
}

static void lab_close(Window* w) {
    if (w->userdata) delete (LabState*)w->userdata;
    w->userdata = 0;
}

} // namespace

void audiolab_launch() {
    int x, y;
    cascade_pos(&x, &y);
    Window* w = g_wm->create_window("AudioLab", x, y, LAB_W, LAB_H);
    if (!w) return;
    LabState* s = new LabState();
    s->generate();
    w->userdata = s;
    w->on_paint = lab_paint;
    w->on_key = lab_key;
    w->on_close = lab_close;
    g_wm->raise(w);
}

} // namespace nefu
