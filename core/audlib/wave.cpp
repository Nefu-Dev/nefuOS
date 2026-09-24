// nefuOS audlib —— WAV 实现 + 自测
#include "audlib/wave.h"
#include <cstdio>
#include <cstring>

namespace nefu {
namespace audx {

void AudioBuf::fade_in(int n) {
    if (n <= 0) return;
    int total = frames();
    if (n > total) n = total;
    for (int i = 0; i < n; i++) {
        double g = (double)i / n;
        for (int c = 0; c < channels; c++)
            data[i * channels + c] = (short)(data[i * channels + c] * g);
    }
}

void AudioBuf::fade_out(int n) {
    if (n <= 0) return;
    int total = frames();
    if (n > total) n = total;
    for (int i = 0; i < n; i++) {
        double g = (double)(n - 1 - i) / n;
        int idx = (total - n + i) * channels;
        for (int c = 0; c < channels; c++)
            data[idx + c] = (short)(data[idx + c] * g);
    }
}

std::vector<unsigned char> WaveIO::to_bytes(const AudioBuf& in) {
    // RIFF 头 44 字节 + 数据
    int data_bytes = (int)in.data.size() * 2;
    std::vector<unsigned char> out(44 + data_bytes);
    // "RIFF"
    out[0] = 'R'; out[1] = 'I'; out[2] = 'F'; out[3] = 'F';
    unsigned chunk = 36 + data_bytes;
    out[4] = chunk & 0xFF; out[5] = (chunk >> 8) & 0xFF;
    out[6] = (chunk >> 16) & 0xFF; out[7] = (chunk >> 24) & 0xFF;
    // "WAVE"
    out[8] = 'W'; out[9] = 'A'; out[10] = 'V'; out[11] = 'E';
    // "fmt "
    out[12] = 'f'; out[13] = 'm'; out[14] = 't'; out[15] = ' ';
    out[16] = 16; out[17] = 0; out[18] = 0; out[19] = 0;   // fmt 块大小 16
    out[20] = 1; out[21] = 0;                               // PCM
    out[22] = (unsigned char)in.channels; out[23] = 0;      // 声道数
    unsigned rate = in.sample_rate;
    out[24] = rate & 0xFF; out[25] = (rate >> 8) & 0xFF;
    out[26] = (rate >> 16) & 0xFF; out[27] = (rate >> 24) & 0xFF;
    unsigned byte_rate = rate * in.channels * 2;
    out[28] = byte_rate & 0xFF; out[29] = (byte_rate >> 8) & 0xFF;
    out[30] = (byte_rate >> 16) & 0xFF; out[31] = (byte_rate >> 24) & 0xFF;
    unsigned block_align = in.channels * 2;
    out[32] = block_align & 0xFF; out[33] = (block_align >> 8) & 0xFF;
    out[34] = 16; out[35] = 0;                              // 位深 16
    // "data"
    out[36] = 'd'; out[37] = 'a'; out[38] = 't'; out[39] = 'a';
    out[40] = data_bytes & 0xFF; out[41] = (data_bytes >> 8) & 0xFF;
    out[42] = (data_bytes >> 16) & 0xFF; out[43] = (data_bytes >> 24) & 0xFF;
    // 采样（小端）
    for (size_t i = 0; i < in.data.size(); i++) {
        unsigned short v = (unsigned short)in.data[i];
        out[44 + i * 2] = v & 0xFF;
        out[44 + i * 2 + 1] = (v >> 8) & 0xFF;
    }
    return out;
}

bool WaveIO::from_bytes(const std::vector<unsigned char>& bytes, AudioBuf& out) {
    if (bytes.size() < 44) return false;
    if (bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'F' || bytes[3] != 'F') return false;
    if (bytes[8] != 'W' || bytes[9] != 'A' || bytes[10] != 'V' || bytes[11] != 'E') return false;
    int channels = bytes[22] | (bytes[23] << 8);
    unsigned rate = bytes[24] | (bytes[25] << 8) | (bytes[26] << 16) | ((unsigned)bytes[27] << 24);
    // 定位 data 块
    size_t pos = 12;
    int data_bytes = 0;
    size_t data_off = 0;
    while (pos + 8 <= bytes.size()) {
        char id0 = (char)bytes[pos], id1 = (char)bytes[pos + 1], id2 = (char)bytes[pos + 2], id3 = (char)bytes[pos + 3];
        unsigned sz = bytes[pos + 4] | (bytes[pos + 5] << 8) | (bytes[pos + 6] << 16) | ((unsigned)bytes[pos + 7] << 24);
        if (id0 == 'd' && id1 == 'a' && id2 == 't' && id3 == 'a') {
            data_bytes = sz;
            data_off = pos + 8;
            break;
        }
        pos += 8 + sz + (sz & 1);   // 块对齐
    }
    if (data_bytes <= 0 || data_off + data_bytes > bytes.size()) return false;
    out.channels = channels;
    out.sample_rate = rate;
    out.data.resize(data_bytes / 2);
    for (int i = 0; i < data_bytes / 2; i++)
        out.data[i] = (short)(bytes[data_off + i * 2] | (bytes[data_off + i * 2 + 1] << 8));
    return true;
}

bool WaveIO::read(const std::string& path, AudioBuf& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 44) { fclose(f); return false; }
    std::vector<unsigned char> bytes((size_t)size);
    if (fread(bytes.data(), 1, (size_t)size, f) != (size_t)size) { fclose(f); return false; }
    fclose(f);
    return from_bytes(bytes, out);
}

bool WaveIO::write(const std::string& path, const AudioBuf& in) {
    std::vector<unsigned char> bytes = to_bytes(in);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    fclose(f);
    return ok;
}

// ---- self test ----
int WaveIO::self_test() {
    int fails = 0;
    // 1. 字节往返
    {
        AudioBuf a(8000, 1);
        a.silence(10);
        a.data[0] = 1000; a.data[1] = -1000; a.data[2] = 30000;
        std::vector<unsigned char> b = to_bytes(a);
        if (b.size() != 44 + 10 * 2) fails++;
        AudioBuf c;
        if (!from_bytes(b, c)) fails++;
        if (c.sample_rate != 8000 || c.channels != 1) fails++;
        if (c.frames() != 10) fails++;
        if (c.data[0] != 1000 || c.data[1] != -1000 || c.data[2] != 30000) fails++;
    }
    // 2. 立体声往返
    {
        AudioBuf a(22050, 2);
        a.silence(5);
        a.data[0] = 111; a.data[1] = 222;   // 第 0 帧 L/R
        a.data[2] = 333; a.data[3] = 444;
        std::vector<unsigned char> b = to_bytes(a);
        AudioBuf c;
        if (!from_bytes(b, c)) fails++;
        if (c.channels != 2 || c.frames() != 5) fails++;
        if (c.data[0] != 111 || c.data[1] != 222 || c.data[3] != 444) fails++;
    }
    // 3. 淡入淡出
    {
        AudioBuf a(100, 1);
        a.silence(10);
        for (int i = 0; i < 10; i++) a.data[i] = 32767;
        a.fade_in(5);
        if (a.data[0] != 0) fails++;                    // 起点 0
        if (a.data[4] < 25000) fails++;                 // 终点接近原值（4/5）
        a.fade_out(5);
        if (a.data[9] != 0) fails++;                    // 终点 0
        if (a.data[5] < 25000) fails++;                 // 起点接近原值（4/5）
        if (a.data[5] > 32767 || a.data[9] < 0) fails++;
    }
    // 4. 坏文件拒绝
    {
        std::vector<unsigned char> bad(20, 0);
        AudioBuf a;
        if (from_bytes(bad, a)) fails++;
        std::vector<unsigned char> bad2(50, 0);
        bad2[0] = 'R'; bad2[1] = 'I'; bad2[2] = 'F'; bad2[3] = 'F';
        bad2[8] = 'X';  // 非 WAVE
        if (from_bytes(bad2, a)) fails++;
    }
    return fails;
}

} // namespace audx
} // namespace nefu
