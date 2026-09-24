// ============================================================================
// nefuOS 3D 图形库 —— ppm 实现
// ============================================================================
#include "ppm.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace nefu {
namespace gfx3d {

int ppm_write(const uint32_t* rgba, int w, int h, uint8_t* buf, int bufsz) {
    int off = 0;
    int n = std::sprintf((char*)buf, "P6\n%d %d\n255\n", w, h);
    if (n < 0 || n >= bufsz) return 0;
    off = n;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = rgba[y * w + x];
            if (off + 3 > bufsz) return off;
            buf[off++] = (c >> 16) & 0xFF;   // R
            buf[off++] = (c >> 8) & 0xFF;    // G
            buf[off++] = c & 0xFF;           // B
        }
    }
    return off;
}

uint32_t* ppm_read(const uint8_t* data, int size, int& w, int& h) {
    w = 0; h = 0;
    if (size < 12) return 0;
    // 头必须以 "P6" 开头
    if (data[0] != 'P' || data[1] != '6') return 0;
    int pos = 2;
    // 跳空白和注释
    auto skip = [&]() {
        while (pos < size) {
            while (pos < size && (data[pos] == ' ' || data[pos] == '\t' ||
                   data[pos] == '\n' || data[pos] == '\r')) pos++;
            if (pos < size && data[pos] == '#') {
                while (pos < size && data[pos] != '\n') pos++;
            } else break;
        }
    };
    skip();
    w = std::atoi((const char*)data + pos);
    while (pos < size && data[pos] != ' ' && data[pos] != '\n') pos++;
    skip();
    h = std::atoi((const char*)data + pos);
    while (pos < size && data[pos] != ' ' && data[pos] != '\n') pos++;
    skip();
    // 最大值（应为 255）
    int maxv = std::atoi((const char*)data + pos);
    if (maxv != 255) return 0;
    while (pos < size && data[pos] != '\n') pos++;
    pos++;   // 跳过换行
    if (pos + w * h * 3 > size) return 0;

    uint32_t* out = new uint32_t[(size_t)w * h];
    for (int i = 0; i < w * h; i++) {
        uint8_t r = data[pos + i * 3 + 0];
        uint8_t g = data[pos + i * 3 + 1];
        uint8_t b = data[pos + i * 3 + 2];
        out[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    return out;
}

int ppm_self_test() {
    int fail = 0;
    const int W = 4, H = 4;
    uint32_t img[W * H];
    for (int i = 0; i < W * H; i++) img[i] = 0xFF808080u;
    img[0] = 0xFFFF0000;   // 红
    img[1] = 0xFF00FF00;   // 绿
    uint8_t buf[256];
    int n = ppm_write(img, W, H, buf, sizeof(buf));
    if (n <= 0) { fail++; return fail; }

    int rw, rh;
    uint32_t* back = ppm_read(buf, n, rw, rh);
    if (!back) { fail++; return fail; }
    if (rw != W || rh != H) fail++;
    if ((back[0] & 0xFF0000) != 0xFF0000) fail++;   // 红
    if ((back[1] & 0x00FF00) != 0x00FF00) fail++;   // 绿
    delete[] back;
    return fail;
}

} // namespace gfx3d
} // namespace nefu
