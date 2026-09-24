// nefuOS 数据序列化与编解码库 —— 音频格式模块实现
#include "audiof.h"
#include "textfmt.h"
#include "binary.h"
#include "imagec.h"
#include <math.h>
#include <cstdio>

namespace nefu {
namespace serialize {

namespace {
// 小端读写
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); }
inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
// 大端读写（AIFF/AU）
inline uint16_t rd16_be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t rd32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
inline void wr16_be(uint8_t* p, uint16_t v) { p[0] = (uint8_t)((v >> 8) & 0xFF); p[1] = (uint8_t)(v & 0xFF); }
inline void wr32_be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF); p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF); p[3] = (uint8_t)(v & 0xFF);
}
// 手动 memcpy（避免 -O2 误优化）
__attribute__((noinline)) void cpy(void* dst, const void* src, int n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    const volatile uint8_t* s = (const volatile uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}
// 读 IEEE 754 小端 float
inline float rd_float_le(const uint8_t* p) {
    uint32_t u = rd32(p);
    float f;
    cpy(&f, &u, 4);
    return f;
}
inline void wr_float_le(uint8_t* p, float f) {
    uint32_t u;
    cpy(&u, &f, 4);
    wr32(p, u);
}
} // namespace

// ============================================================================
// AudioBuf
// ============================================================================
void AudioBuf::alloc(int rate, int ch, int nframes) {
    free_buf();
    hz = rate; channels = ch; frames = nframes;
    samples = new float[(size_t)frames * channels];
    own = true;
    {
        // volatile 防止 -O2 把清零循环误转成 __builtin_memset 算错长度
        volatile float* d = samples;
        for (int i = 0; i < frames * channels; i++) d[i] = 0.0f;
    }
}
void AudioBuf::free_buf() {
    if (samples && own) delete[] samples;
    samples = 0; frames = 0;
}
float AudioBuf::get(int frame, int ch) const {
    if (frame < 0 || frame >= frames) return 0;
    return samples[(frame * channels + ch) * 1];
}
void AudioBuf::set(int frame, int ch, float v) {
    if (frame < 0 || frame >= frames) return;
    samples[frame * channels + ch] = v;
}

// ============================================================================
// WAV
// ============================================================================
bool wav_read_info(const uint8_t* data, int len, WavInfo& info) {
    if (len < 44) return false;
    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F') return false;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E') return false;
    int pos = 12;
    bool got_fmt = false, got_data = false;
    while (pos + 8 <= len) {
        uint32_t ckid = rd32(data + pos);
        uint32_t cksz = rd32(data + pos + 4);
        const uint8_t* body = data + pos + 8;
        if (ckid == 0x20746D66 /*'fmt '*/) {
            info.format = rd16(body);
            info.channels = rd16(body + 2);
            info.sample_rate = (int)rd32(body + 4);
            info.byte_rate = (int)rd32(body + 8);
            info.block_align = rd16(body + 12);
            info.bits = rd16(body + 14);
            got_fmt = true;
        } else if (ckid == 0x61746164 /*'data'*/) {
            info.data_bytes = (int)cksz;
            got_data = true;
        }
        pos += 8 + (cksz + 1) & ~1;   // chunk 对齐到偶数字节
        if (got_fmt && got_data) break;
    }
    return got_fmt && got_data;
}

bool wav_read(const uint8_t* data, int len, AudioBuf& out) {
    WavInfo info;
    if (!wav_read_info(data, len, info)) return false;
    if (info.channels < 1 || info.channels > 2) return false;
    int bytes_per_sample = info.bits / 8;
    int frames = info.data_bytes / (info.channels * bytes_per_sample);
    out.alloc(info.sample_rate, info.channels, frames);
    // 找 data chunk 体位置
    int pos = 12;
    const uint8_t* dbody = 0;
    while (pos + 8 <= len) {
        uint32_t ckid = rd32(data + pos);
        uint32_t cksz = rd32(data + pos + 4);
        if (ckid == 0x61746164) { dbody = data + pos + 8; break; }
        pos += 8 + (cksz + 1) & ~1;
    }
    if (!dbody) return false;
    for (int i = 0; i < frames * info.channels; i++) {
        const uint8_t* p = dbody + i * bytes_per_sample;
        float v = 0;
        if (info.format == 3) {   // IEEE float
            v = rd_float_le(p);
        } else if (info.bits == 8) {
            v = ((int)p[0] - 128) / 128.0f;       // 8 位 PCM 无符号
        } else if (info.bits == 16) {
            int16_t s = (int16_t)rd16(p);
            v = s / 32768.0f;
        } else if (info.bits == 24) {
            int32_t s = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);
            if (s & 0x800000) s |= ~0xFFFFFF;    // 符号扩展
            v = s / 8388608.0f;
        } else if (info.bits == 32) {
            int32_t s = (int32_t)rd32(p);
            v = s / 2147483648.0f;
        }
        out.samples[i] = v;
    }
    return true;
}

int wav_encoded_size(const AudioBuf& audio, int bits) {
    int data = audio.total_samples() * (bits / 8);
    return 12 + 24 + 8 + data;   // RIFF头12 + fmt chunk24 + data chunk8 + data
}

int wav_write(const AudioBuf& audio, uint8_t* out, int out_cap, int bits) {
    int data = audio.total_samples() * (bits / 8);
    int total = 12 + 24 + 8 + data;
    if (out_cap < total) return -1;
    // RIFF
    out[0] = 'R'; out[1] = 'I'; out[2] = 'F'; out[3] = 'F';
    wr32(out + 4, (uint32_t)(total - 8));
    out[8] = 'W'; out[9] = 'A'; out[10] = 'V'; out[11] = 'E';
    // fmt chunk
    out[12] = 'f'; out[13] = 'm'; out[14] = 't'; out[15] = ' ';
    wr32(out + 16, 16);                       // fmt 大小
    wr16(out + 20, 1);                        // PCM
    wr16(out + 22, (uint16_t)audio.channels);
    wr32(out + 24, (uint32_t)audio.hz);
    int byterate = audio.hz * audio.channels * bits / 8;
    wr32(out + 28, (uint32_t)byterate);
    wr16(out + 32, (uint16_t)(audio.channels * bits / 8));
    wr16(out + 34, (uint16_t)bits);
    // data chunk
    out[36] = 'd'; out[37] = 'a'; out[38] = 't'; out[39] = 'a';
    wr32(out + 40, (uint32_t)data);
    uint8_t* p = out + 44;
    for (int i = 0; i < audio.total_samples(); i++) {
        float s = audio.samples[i];
        if (bits == 16) {
            int16_t v = (int16_t)(s * 32767.0f);
            wr16(p, (uint16_t)v); p += 2;
        } else if (bits == 8) {
            int v = (int)(s * 127.0f) + 128;
            if (v < 0) v = 0; if (v > 255) v = 255;
            *p++ = (uint8_t)v;
        } else if (bits == 32) {
            int32_t v = (int32_t)(s * 2147483647.0f);
            wr32(p, (uint32_t)v); p += 4;
        }
    }
    return total;
}

int wav_self_test() {
    int fail = 0;
    // 生成 0.1 秒 440Hz 正弦波（44100Hz 16位 单声道）
    AudioBuf a;
    int frames = 4410;   // 0.1 秒
    a.alloc(44100, 1, frames);
    for (int i = 0; i < frames; i++)
        a.set(i, 0, 0.5f * sinf(2.0f * 3.14159265f * 440.0f * i / 44100.0f));
    int sz = wav_encoded_size(a, 16);
    uint8_t* buf = new uint8_t[sz];
    int n = wav_write(a, buf, sz, 16);
    if (n != sz) fail++;
    // 验证文件头
    if (buf[0] != 'R' || buf[8] != 'W') fail++;
    WavInfo info;
    if (!wav_read_info(buf, n, info)) fail++;
    else {
        if (info.sample_rate != 44100) fail++;
        if (info.channels != 1) fail++;
        if (info.bits != 16) fail++;
        if (info.data_bytes != frames * 2) fail++;
    }
    AudioBuf back;
    if (!wav_read(buf, n, back)) fail++;
    else {
        if (back.frames != frames) fail++;
        // 校验首尾样本接近
        if (back.get(0, 0) > 0.01f) fail++;   // sin(0)=0
        if (back.get(100, 0) < -0.5f || back.get(100, 0) > 0.5f) fail++;
        back.free_buf();
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// AIFF（大端，只读）
// ============================================================================
bool aiff_read(const uint8_t* data, int len, AudioBuf& out) {
    if (len < 12) return false;
    if (data[0] != 'F' || data[1] != 'O' || data[2] != 'R' || data[3] != 'M') return false;
    if (data[8] != 'A' || data[9] != 'I' || data[10] != 'F' || data[11] != 'F') return false;
    int pos = 12;
    int channels = 0, frames = 0, bits = 0, rate = 0;
    const uint8_t* snd = 0;
    int snd_len = 0;
    while (pos + 8 <= len) {
        uint32_t ckid = rd32_be(data + pos);
        uint32_t cksz = rd32_be(data + pos + 4);
        const uint8_t* body = data + pos + 8;
        if (ckid == 0x434F4D4D /*'COMM'*/) {
            channels = rd16_be(body);
            frames = (int)rd32_be(body + 2);
            bits = rd16_be(body + 6);
            // 采样率是 80 位扩展精度，取整数部分（后4字节）
            rate = (int)rd32_be(body + 8 + 6);
        } else if (ckid == 0x53534E44 /*'SSND'*/) {
            snd = body + 8;   // 跳过 8 字节偏移块
            snd_len = cksz - 8;
        }
        pos += 8 + cksz;
        if (cksz & 1) pos++;
    }
    if (!snd || channels <= 0 || frames <= 0) return false;
    out.alloc(rate, channels, frames);
    int bps = bits / 8;
    for (int i = 0; i < frames * channels; i++) {
        const uint8_t* p = snd + i * bps;
        float v = 0;
        if (bits == 16) {
            int16_t s = (int16_t)rd16_be(p);
            v = s / 32768.0f;
        } else if (bits == 8) {
            v = ((int)p[0] - 128) / 128.0f;
        }
        out.samples[i] = v;
    }
    (void)snd_len;
    return true;
}

int aiff_self_test() {
    int fail = 0;
    // 构造最小 AIFF: FORM + COMM + SSND
    // COMM: ch=1, frames=2, bits=16, rate=44100(粗略用扩展精度整数)
    uint8_t buf[64 + 8];
    // 手工组装
    int p = 0;
    buf[p++]='F';buf[p++]='O';buf[p++]='R';buf[p++]='M';
    wr32_be(buf+p, 40); p+=4;
    buf[p++]='A';buf[p++]='I';buf[p++]='F';buf[p++]='F';
    // COMM chunk
    buf[p++]='C';buf[p++]='O';buf[p++]='M';buf[p++]='M';
    wr32_be(buf+p, 18); p+=4;
    wr16_be(buf+p, 1); p+=2;           // channels
    wr32_be(buf+p, 2); p+=4;           // frames
    wr16_be(buf+p, 16); p+=2;          // bits
    // 80位扩展精度采样率：44100 = (0x400E <<16) | 0xAC440000 的简化表示
    // 用一个 80 位块，其中我们读 body+8+6 = 后4字节 = 0xAC44 -> 44100
    wr16_be(buf+p, 0x400E); p+=2;      // exponent
    wr32_be(buf+p, 0xAC440000); p+=4;  // mantissa 高4字节
    p += 4;                             // mantissa 低4字节(我们没写满,跳过)
    // 对齐
    // SSND chunk
    buf[p++]='S';buf[p++]='S';buf[p++]='N';buf[p++]='D';
    wr32_be(buf+p, 8+4); p+=4;
    wr32_be(buf+p, 0); p+=4;           // offset
    wr32_be(buf+p, 0); p+=4;           // block size
    // 2 个 int16 样本
    wr16_be(buf+p, 16000); p+=2;
    wr16_be(buf+p, -16000 & 0xFFFF); p+=2;
    AudioBuf a;
    if (!aiff_read(buf, p, a)) fail++;
    else {
        if (a.frames != 2) fail++;
        if (a.channels != 1) fail++;
        a.free_buf();
    }
    return fail;
}

// ============================================================================
// AU（大端）
// ============================================================================
int au_encoded_size(const AudioBuf& audio) {
    return 24 + audio.total_samples() * 2;   // 16 位线性
}

bool au_read(const uint8_t* data, int len, AudioBuf& out) {
    if (len < 24) return false;
    if (data[0] != '.' || data[1] != 's' || data[2] != 'n' || data[3] != 'd') return false;
    uint32_t hdr_size = rd32_be(data + 4);
    uint32_t data_size = rd32_be(data + 8);
    uint32_t encoding = rd32_be(data + 12);   // 3 = 16位 linear
    uint32_t rate = rd32_be(data + 16);
    uint32_t ch = rd32_be(data + 20);
    if (encoding != 3) return false;
    const uint8_t* snd = data + hdr_size;
    int frames = (int)data_size / (ch * 2);
    out.alloc((int)rate, (int)ch, frames);
    for (int i = 0; i < frames * (int)ch; i++) {
        int16_t v = (int16_t)rd16_be(snd + i * 2);
        out.samples[i] = v / 32768.0f;
    }
    return true;
}

int au_write(const AudioBuf& audio, uint8_t* out, int out_cap) {
    int total = au_encoded_size(audio);
    if (out_cap < total) return -1;
    out[0]='.';out[1]='s';out[2]='n';out[3]='d';
    wr32_be(out+4, 24);
    wr32_be(out+8, (uint32_t)(audio.total_samples()*2));
    wr32_be(out+12, 3);   // 16位 linear
    wr32_be(out+16, (uint32_t)audio.hz);
    wr32_be(out+20, (uint32_t)audio.channels);
    uint8_t* p = out + 24;
    for (int i = 0; i < audio.total_samples(); i++) {
        int16_t v = (int16_t)(audio.samples[i] * 32767.0f);
        wr16_be(p, (uint16_t)v); p += 2;
    }
    return total;
}

int au_self_test() {
    int fail = 0;
    AudioBuf a;
    a.alloc(22050, 1, 100);
    for (int i = 0; i < 100; i++) a.set(i, 0, 0.3f * sinf(i * 0.1f));
    int sz = au_encoded_size(a);
    uint8_t* buf = new uint8_t[sz];
    int n = au_write(a, buf, sz);
    if (n != sz) fail++;
    if (buf[0] != '.' || buf[1] != 's') fail++;
    AudioBuf back;
    if (!au_read(buf, n, back)) fail++;
    else {
        if (back.frames != 100) fail++;
        if (back.hz != 22050) fail++;
        back.free_buf();
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// raw PCM 转换
// ============================================================================
void raw_i16_to_float(const int16_t* in, float* out, int n) {
    for (int i = 0; i < n; i++) out[i] = in[i] / 32768.0f;
}
void raw_float_to_i16(const float* in, int16_t* out, int n) {
    for (int i = 0; i < n; i++) {
        float v = in[i];
        if (v > 1.0f) v = 1.0f; if (v < -1.0f) v = -1.0f;
        out[i] = (int16_t)(v * 32767.0f);
    }
}


// ============================================================================
// 扩展测试：24/32 位 WAV、立体声、不同采样率
// ============================================================================
int audiof_extra_self_test() {
    int fail = 0;
    // 16 位立体声
    {
        AudioBuf a;
        a.alloc(22050, 2, 100);
        for (int i = 0; i < 100; i++) {
            a.set(i, 0, 0.3f * sinf(i * 0.2f));
            a.set(i, 1, 0.3f * cosf(i * 0.2f));
        }
        int sz = wav_encoded_size(a, 16);
        uint8_t* buf = new uint8_t[sz];
        int n = wav_write(a, buf, sz, 16);
        if (n != sz) fail++;
        AudioBuf back;
        if (!wav_read(buf, n, back)) fail++;
        else {
            if (back.channels != 2) fail++;
            if (back.frames != 100) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    // 8 位 WAV
    {
        AudioBuf a;
        a.alloc(8000, 1, 50);
        for (int i = 0; i < 50; i++) a.set(i, 0, 0.5f);
        int sz = wav_encoded_size(a, 8);
        uint8_t* buf = new uint8_t[sz];
        int n = wav_write(a, buf, sz, 8);
        if (n != sz) fail++;
        WavInfo info;
        if (!wav_read_info(buf, n, info)) fail++;
        else if (info.bits != 8) fail++;
        AudioBuf back;
        if (!wav_read(buf, n, back)) fail++;
        else { back.free_buf(); }
        delete[] buf;
    }
    // 32 位 WAV
    {
        AudioBuf a;
        a.alloc(44100, 1, 100);
        for (int i = 0; i < 100; i++) a.set(i, 0, (float)i / 100.0f - 0.5f);
        int sz = wav_encoded_size(a, 32);
        uint8_t* buf = new uint8_t[sz];
        int n = wav_write(a, buf, sz, 32);
        if (n != sz) fail++;
        AudioBuf back;
        if (!wav_read(buf, n, back)) fail++;
        else {
            if (back.frames != 100) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    return fail;
}


// ============================================================================
// 多频率正弦波：生成 440/880Hz，写入 WAV 后读回，验证 RMS 能量存在。
// ============================================================================
int audiof_freq_self_test() {
    int fail = 0;
    float freqs[2] = {440.0f, 880.0f};
    for (int fi = 0; fi < 2; fi++) {
        AudioBuf a;
        int frames = 2205;   // 0.05 秒 @44100
        a.alloc(44100, 1, frames);
        for (int i = 0; i < frames; i++)
            a.set(i, 0, 0.6f * sinf(2.0f * 3.14159265f * freqs[fi] * i / 44100.0f));
        int sz = wav_encoded_size(a, 16);
        uint8_t* buf = new uint8_t[sz];
        wav_write(a, buf, sz, 16);
        AudioBuf back;
        if (!wav_read(buf, sz, back)) { fail++; delete[] buf; continue; }
        // 计算 RMS
        double sum = 0;
        for (int i = 0; i < back.frames; i++) sum += (double)back.get(i, 0) * back.get(i, 0);
        double rms = sqrt(sum / back.frames);
        if (rms < 0.2) fail++;     // 应有明显能量
        back.free_buf();
        delete[] buf;
    }
    return fail;
}


// ============================================================================
// WAV 位深矩阵：8/16/32 位、单/双声道、不同采样率两两往返。
// ============================================================================
int wav_depth_matrix() {
    int fail = 0;
    int depths[3] = {8, 16, 32};
    int chans[2] = {1, 2};
    int rates[2] = {8000, 44100};
    for (int di = 0; di < 3; di++)
        for (int ci = 0; ci < 2; ci++)
            for (int ri = 0; ri < 2; ri++) {
                AudioBuf a;
                a.alloc(rates[ri], chans[ci], 50);
                for (int i = 0; i < 50; i++)
                    for (int c = 0; c < chans[ci]; c++)
                        a.set(i, c, 0.3f * sinf(i * 0.1f + c));
                int sz = wav_encoded_size(a, depths[di]);
                uint8_t* buf = new uint8_t[sz];
                int n = wav_write(a, buf, sz, depths[di]);
                if (n != sz) { fail++; delete[] buf; continue; }
                WavInfo info;
                if (!wav_read_info(buf, n, info)) fail++;
                else {
                    if (info.bits != depths[di]) fail++;
                    if (info.channels != chans[ci]) fail++;
                    if (info.sample_rate != rates[ri]) fail++;
                }
                AudioBuf back;
                if (wav_read(buf, n, back)) {
                    if (back.frames != 50) fail++;
                    if (back.channels != chans[ci]) fail++;
                    back.free_buf();
                } else fail++;
                delete[] buf;
            }
    return fail;
}

// ============================================================================
// WAV 文件头已知向量：验证魔数字节布局。
// ============================================================================
int wav_header_vectors() {
    int fail = 0;
    AudioBuf a;
    a.alloc(44100, 1, 1);
    int sz = wav_encoded_size(a, 16);
    uint8_t* buf = new uint8_t[sz];
    wav_write(a, buf, sz, 16);
    // "RIFF"
    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F') fail++;
    // "WAVE"
    if (buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') fail++;
    // "fmt "
    if (buf[12] != 'f' || buf[13] != 'm' || buf[14] != 't' || buf[15] != ' ') fail++;
    // "data"
    if (buf[36] != 'd' || buf[37] != 'a' || buf[38] != 't' || buf[39] != 'a') fail++;
    delete[] buf;
    return fail;
}

// ============================================================================
// AU 头已知向量
// ============================================================================
int au_header_vectors() {
    int fail = 0;
    AudioBuf a;
    a.alloc(8000, 1, 10);
    int sz = au_encoded_size(a);
    uint8_t* buf = new uint8_t[sz];
    au_write(a, buf, sz);
    if (buf[0] != '.' || buf[1] != 's' || buf[2] != 'n' || buf[3] != 'd') fail++;
    // encoding=3 (16位)
    uint32_t enc = ((uint32_t)buf[12] << 24) | ((uint32_t)buf[13] << 16) |
                   ((uint32_t)buf[14] << 8) | buf[15];
    if (enc != 3) fail++;
    delete[] buf;
    return fail;
}


// ============================================================================
// 音频工具：立体声->单声道混缩、归一化、增益（真实采样运算）
// ============================================================================

// 立体声混缩为单声道（两路平均）
static void audio_mixdown_to_mono(AudioBuf& a) {
    if (a.channels != 2) return;
    for (int i = 0; i < a.frames; i++) {
        float l = a.get(i, 0);
        float r = a.get(i, 1);
        a.set(i, 0, (l + r) * 0.5f);
    }
    a.channels = 1;
}

// 简单增益：整体放大 g 倍
static void audio_gain(AudioBuf& a, float g) {
    for (int i = 0; i < a.frames; i++)
        for (int c = 0; c < a.channels; c++)
            a.set(i, c, a.get(i, c) * g);
}

// 找峰值
static float audio_peak(const AudioBuf& a) {
    float pk = 0.0f;
    for (int i = 0; i < a.frames; i++)
        for (int c = 0; c < a.channels; c++) {
            float v = a.get(i, c);
            float av = v < 0 ? -v : v;
            if (av > pk) pk = av;
        }
    return pk;
}

int audio_utils_test() {
    int fail = 0;
    AudioBuf a;
    a.alloc(8000, 2, 10);
    for (int i = 0; i < 10; i++) {
        a.set(i, 0, 0.5f);   // 左 0.5
        a.set(i, 1, 0.3f);   // 右 0.3
    }
    float before = audio_peak(a);
    if (before < 0.5f) fail++;
    audio_gain(a, 2.0f);
    if (a.get(0, 0) < 0.99f || a.get(0, 0) > 1.01f) fail++;  // 1.0
    audio_mixdown_to_mono(a);
    if (a.channels != 1) fail++;
    // 左=1.0 右=0.6 -> 0.8
    if (a.get(0, 0) < 0.79f || a.get(0, 0) > 0.81f) fail++;
    return fail;
}

// ============================================================================
// raw PCM 互转往返
// ============================================================================
int raw_pcm_roundtrip_test() {
    int fail = 0;
    const int N = 8;
    float fin[N] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 0.25f, -0.25f, 0.75f};
    int16_t i16[N];
    raw_float_to_i16(fin, i16, N);
    float back[N];
    raw_i16_to_float(i16, back, N);
    for (int i = 0; i < N; i++) {
        float d = back[i] - fin[i];
        if (d < 0) d = -d;
        if (d > 0.01f) fail++;   // 量化误差容差
    }
    return fail;
}


// ============================================================================
// AU 格式往返：写 AU 再读回
// ============================================================================
int au_roundtrip_test() {
    int fail = 0;
    AudioBuf a;
    a.alloc(22050, 1, 100);
    for (int i = 0; i < 100; i++) a.set(i, 0, 0.3f * sinf(i * 0.05f));
    int sz = au_encoded_size(a);
    uint8_t* buf = new uint8_t[sz];
    au_write(a, buf, sz);
    AudioBuf back;
    if (!au_read(buf, sz, back)) fail++;
    else {
        
        if (back.channels != 1) fail++;
        back.free_buf();
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// 立体声 WAV 往返：两路独立正弦
// ============================================================================
int wav_stereo_roundtrip_test() {
    int fail = 0;
    AudioBuf a;
    a.alloc(8000, 2, 20);
    for (int i = 0; i < 20; i++) {
        a.set(i, 0, 0.5f * sinf(i * 0.3f));
        a.set(i, 1, 0.25f * sinf(i * 0.6f));
    }
    int sz = wav_encoded_size(a, 16);
    uint8_t* buf = new uint8_t[sz];
    wav_write(a, buf, sz, 16);
    AudioBuf back;
    if (!wav_read(buf, sz, back)) fail++;
    else {
        if (back.channels != 2) fail++;
        if (back.frames != 20) fail++;
        back.free_buf();
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// WAV 拒绝损坏/截断头
// ============================================================================
int wav_truncated_test() {
    int fail = 0;
    uint8_t buf[16] = {'R','I','F','F',0,0,0,0,'W','A','V','E'};
    AudioBuf a;
    if (wav_read(buf, 16, a)) fail++;   // 太短，应失败
    return fail;
}

int audiof_self_test() {
    int f = 0;
    f += wav_self_test();
    f += audiof_extra_self_test();
    f += audiof_freq_self_test();
    f += wav_depth_matrix();
    f += wav_header_vectors();
    f += au_header_vectors();
    f += audio_utils_test();
    f += raw_pcm_roundtrip_test();
    f += au_roundtrip_test();
    f += wav_stereo_roundtrip_test();
    f += wav_truncated_test();
    f += aiff_self_test();
    f += au_self_test();
    // raw 转换
    {
        float fi[3] = {0.5f, -0.5f, 0.0f};
        int16_t o[3];
        raw_float_to_i16(fi, o, 3);
        float b[3];
        raw_i16_to_float(o, b, 3);
        if (b[0] < 0.49f || b[0] > 0.51f) f++;
        if (b[1] > -0.49f || b[1] < -0.51f) f++;
    }
    return f;
}


// ============================================================================
// 汇总入口：运行所有子模块 self_test，返回失败总数
// ============================================================================
int serialize_self_test() {
    int fail = 0;
    { int f=textfmt_self_test(); fprintf(stderr, "textfmt=%d\n", f); fail += f; }   // CSV/INI/XML/TOML/JSON5/properties/env/s-expr
    { int f=binary_self_test(); fprintf(stderr, "binary=%d\n", f); fail += f; }    // MessagePack/Bencode/UBJSON/CBOR/BSON/varint
    { int f=imagec_self_test(); fprintf(stderr, "imagec=%d\n", f); fail += f; }    // BMP/TGA/PPM/QOI/PCX/ICO
    { int f=audiof_self_test(); fprintf(stderr, "audiof=%d\n", f); fail += f; }    // WAV/AIFF/AU/raw PCM
    return fail;
}
} // namespace serialize
} // namespace nefu
