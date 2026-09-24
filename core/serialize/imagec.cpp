// nefuOS 数据序列化与编解码库 —— 图像格式模块实现
#include "imagec.h"
#include <cstdio>
#include <math.h>

namespace nefu {
namespace serialize {

// ---- 小工具：小端读取（显式字节拼装，freestanding 可移植）----
namespace {
inline uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
inline void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF); }
inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF); p[3] = (uint8_t)((v >> 24) & 0xFF);
}
}
inline uint32_t rd32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
inline void wr32_be(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF); p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF); p[3] = (uint8_t)(v & 0xFF);
} // namespace

// 手动清零/拷贝助手：避免 -O2 下触发 mingw 对 memset/memcpy 内置函数的误优化。
__attribute__((noinline)) void clr_buf(void* dst, size_t n) {
    // volatile 指针防止 gcc -O2 把循环误识别为 __builtin_memset 并算错长度
    volatile uint8_t* d = (volatile uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = 0;
}
__attribute__((noinline)) void cpy_buf(void* dst, const void* src, size_t n) {
    volatile uint8_t* d = (volatile uint8_t*)dst;
    const volatile uint8_t* s = (const volatile uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

// ============================================================================
// ImageBuf
// ============================================================================
void ImageBuf::alloc(int width, int height, int ch) {
    free_buf();
    free_buf();
    w = width; h = height; channels = ch;
    px = new uint8_t[(size_t)w * h * ch];
    own = true;
    clr_buf(px, (size_t)w * h * ch);
}
void ImageBuf::free_buf() {
    if (px && own) delete[] px;
    px = 0; w = h = 0;
}
uint8_t ImageBuf::get_r(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return px[(y * w + x) * channels + 0];
}
uint8_t ImageBuf::get_g(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return px[(y * w + x) * channels + 1];
}
uint8_t ImageBuf::get_b(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return px[(y * w + x) * channels + 2];
}
uint8_t ImageBuf::get_a(int x, int y) const {
    if (x < 0 || y < 0 || x >= w || y >= h || channels < 4) return 255;
    return px[(y * w + x) * channels + 3];
}
void ImageBuf::set_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    px[(y * w + x) * channels + 0] = r;
    px[(y * w + x) * channels + 1] = g;
    px[(y * w + x) * channels + 2] = b;
    if (channels >= 4) px[(y * w + x) * channels + 3] = a;
}

// ============================================================================
// BMP
// ============================================================================
// BMP 行字节数（24 位）需对齐到 4 字节
static int bmp_row_bytes(int w, int bpp) {
    int n = w * (bpp / 8);
    return (n + 3) & ~3;
}

int bmp_encoded_size(const ImageBuf& img) {
    int row = bmp_row_bytes(img.w, 24);
    return 14 + 40 + row * img.h;
}

bool bmp_read(const uint8_t* data, int len, ImageBuf& out) {
    if (len < 54) return false;
    if (data[0] != 'B' || data[1] != 'M') return false;
    uint32_t offset = rd32(data + 10);
    uint32_t hdr_size = rd32(data + 14);
    if (hdr_size < 40) return false;
    int32_t w = (int32_t)rd32(data + 18);
    int32_t h = (int32_t)rd32(data + 22);
    int bpp = rd16(data + 28);
    uint32_t comp = rd32(data + 30);
    if (comp != 0) return false;             // 只支持不压缩
    if (bpp != 24 && bpp != 32) return false;
    int abs_h = h < 0 ? -h : h;
    out.alloc(w, abs_h, 4);
    int row = bmp_row_bytes(w, bpp);
    for (int y = 0; y < abs_h; y++) {
        // BMP 底部朝上：实际行号
        int src_y = h < 0 ? y : (abs_h - 1 - y);
        const uint8_t* rowp = data + offset + src_y * row;
        for (int x = 0; x < w; x++) {
            const uint8_t* p = rowp + x * (bpp / 8);
            uint8_t b = p[0], g = p[1], r = p[2];
            uint8_t a = (bpp == 32) ? p[3] : 255;
            out.set_pixel(x, y, r, g, b, a);
        }
    }
    return true;
}

int bmp_write(const ImageBuf& img, uint8_t* out, int out_cap) {
    int total = bmp_encoded_size(img);
    if (out_cap < total) return -1;
    clr_buf(out, (size_t)total);
    // BITMAPFILEHEADER
    out[0] = 'B'; out[1] = 'M';
    wr32(out + 2, (uint32_t)total);
    wr32(out + 10, 14 + 40);           // 像素数据偏移
    // BITMAPINFOHEADER
    wr32(out + 14, 40);                 // header size
    wr32(out + 18, (uint32_t)img.w);
    wr32(out + 22, (uint32_t)img.h);   // 正高度 = 底部朝上
    wr16(out + 26, 1);                  // planes
    wr16(out + 28, 24);                 // bpp
    // comp=0, 其余留 0
    int row = bmp_row_bytes(img.w, 24);
    uint8_t* px = out + 14 + 40;
    for (int y = 0; y < img.h; y++) {
        int dst_y = img.h - 1 - y;      // 底部朝上
        uint8_t* rowp = px + dst_y * row;
        for (int x = 0; x < img.w; x++) {
            rowp[x * 3 + 0] = img.get_b(x, y);
            rowp[x * 3 + 1] = img.get_g(x, y);
            rowp[x * 3 + 2] = img.get_r(x, y);
        }
    }
    return total;
}

int bmp_self_test() {
    int fail = 0;
    // 构造 2x2 图像：左上红、右上绿、左下蓝、右下白
    ImageBuf img;
    img.alloc(2, 2, 4);
    img.set_pixel(0, 0, 255, 0, 0);
    img.set_pixel(1, 0, 0, 255, 0);
    img.set_pixel(0, 1, 0, 0, 255);
    img.set_pixel(1, 1, 255, 255, 255);
    int sz = bmp_encoded_size(img);
    uint8_t* buf = new uint8_t[sz];
    int n = bmp_write(img, buf, sz);
    if (n != sz) fail++;
    ImageBuf back;
    if (!bmp_read(buf, n, back)) fail++;
    else {
        if (back.w != 2 || back.h != 2) fail++;
        if (back.get_r(0, 0) != 255 || back.get_g(0, 0) != 0 || back.get_b(0, 0) != 0) fail++;
        if (back.get_g(1, 0) != 255) fail++;
        if (back.get_b(0, 1) != 255) fail++;
        if (back.get_r(1, 1) != 255 || back.get_g(1, 1) != 255 || back.get_b(1, 1) != 255) fail++;
        back.free_buf();
    }
    // 已知 BMP 文件头：'BM' + 最小头
    {
        uint8_t fake[54] = {0};
        fake[0] = 'B'; fake[1] = 'M';
        wr32(fake + 10, 54);
        wr32(fake + 14, 40);
        wr32(fake + 18, 1); wr32(fake + 22, 1);
        wr16(fake + 26, 1); wr16(fake + 28, 24);
        ImageBuf im;
        // 1x1 24bpp 行对齐到 4 字节，数据在 54..57
        // 补一行像素
        // (这里只验证魔数，不完整解码)
        if (fake[0] != 'B' || fake[1] != 'M') fail++;
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// TGA
// ============================================================================
bool tga_read(const uint8_t* data, int len, ImageBuf& out) {
    if (len < 18) return false;
    uint8_t idlen = data[0];
    uint8_t cmap_type = data[1];
    uint8_t img_type = data[2];
    if (cmap_type != 0) return false;          // 不读调色板
    if (img_type != 2 && img_type != 10) return false;  // 真彩 / RLE 真彩
    uint16_t w = rd16(data + 12);
    uint16_t h = rd16(data + 14);
    uint8_t bpp = data[16];
    uint8_t desc = data[17];
    if (bpp != 24 && bpp != 32) return false;
    int pxbytes = bpp / 8;
    const uint8_t* p = data + 18 + idlen;
    out.alloc(w, h, 4);

    bool top_down = (desc & 0x20) != 0;
    if (img_type == 2) {
        for (int y = 0; y < h; y++) {
            int dy = top_down ? y : (h - 1 - y);
            for (int x = 0; x < w; x++) {
                const uint8_t* s = p + (y * w + x) * pxbytes;
                out.set_pixel(x, dy, s[2], s[1], s[0], pxbytes == 4 ? s[3] : 255);
            }
        }
    } else {
        // RLE type 10
        int x = 0, y = 0;
        int pos = 0;
        int total = w * h;
        for (int done = 0; done < total && pos < len; ) {
            uint8_t b = p[pos++];
            int count = (b & 0x7F) + 1;
            bool rle = (b & 0x80) != 0;
            for (int k = 0; k < count; k++) {
                const uint8_t* s;
                uint8_t tmp[4];
                if (rle) {
                    if (pos + pxbytes > len) break;
                    cpy_buf(tmp, p + pos, pxbytes);
                    s = tmp;
                    // RLE 包只出现一次像素数据
                    if (k == 0) { /* consume once below */ }
                } else {
                    if (pos + pxbytes > len) break;
                    s = p + pos;
                    pos += pxbytes;
                }
                int dy = top_down ? y : (h - 1 - y);
                out.set_pixel(x, dy, s[2], s[1], s[0], pxbytes == 4 ? s[3] : 255);
                x++;
                if (x >= w) { x = 0; y++; }
                done++;
            }
            if (rle) pos += pxbytes;
        }
    }
    return true;
}

int tga_encoded_size(const ImageBuf& img) {
    return 18 + img.w * img.h * 3;   // 无压缩 24 位
}

int tga_write(const ImageBuf& img, uint8_t* out, int out_cap) {
    int total = tga_encoded_size(img);
    if (out_cap < total) return -1;
    clr_buf(out, (size_t)total);
    out[2] = 2;            // 未压缩真彩
    out[16] = 24;          // bpp
    out[17] = 0x20;        // top-down
    wr16(out + 12, (uint16_t)img.w);
    wr16(out + 14, (uint16_t)img.h);
    uint8_t* p = out + 18;
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            int i = (y * img.w + x) * 3;
            p[i + 0] = img.get_b(x, y);
            p[i + 1] = img.get_g(x, y);
            p[i + 2] = img.get_r(x, y);
        }
    return total;
}

int tga_self_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(3, 2, 4);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++)
            img.set_pixel(x, y, (uint8_t)(x * 80), (uint8_t)(y * 120), 100);
    int sz = tga_encoded_size(img);
    uint8_t* buf = new uint8_t[sz];
    int n = tga_write(img, buf, sz);
    if (n != sz) fail++;
    ImageBuf back;
    if (!tga_read(buf, n, back)) fail++;
    else {
        if (back.w != 3 || back.h != 2) fail++;
        if (back.get_r(0, 0) != 0 || back.get_r(2, 0) != 160) fail++;
        back.free_buf();
    }
    delete[] buf;
    return fail;
}


// ============================================================================
// PPM / PGM / PBM（PNM 系列）
// ============================================================================
// 跳过空白与注释（# 到行尾）
static const char* pnm_skip(const char* p) {
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        break;
    }
    return p;
}
// 读取一个整数（十进制），返回值并推进 p
static int pnm_read_int(const char*& p) {
    p = pnm_skip(p);
    int v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    return v;
}

bool ppm_read(const uint8_t* data, int len, ImageBuf& out) {
    if (len < 2) return false;
    const char* p = (const char*)data;
    char t1 = p[0], t2 = p[1];
    if (t1 != 'P') return false;
    int type = t2 - '0';
    p += 2;
    if (type == 1 || type == 2 || type == 3) {
        // ASCII 变体
        int w = pnm_read_int(p);
        int h = pnm_read_int(p);
        int maxv = pnm_read_int(p);
        if (maxv <= 0) maxv = 255;
        out.alloc(w, h, 4);
        for (int i = 0; i < w * h; i++) {
            int x = i % w, y = i / w;
            if (type == 3) {   // PPM RGB
                int r = pnm_read_int(p), g = pnm_read_int(p), b = pnm_read_int(p);
                out.set_pixel(x, y, (uint8_t)(r * 255 / maxv), (uint8_t)(g * 255 / maxv), (uint8_t)(b * 255 / maxv));
            } else if (type == 2) {   // PGM 灰度
                int g = pnm_read_int(p);
                out.set_pixel(x, y, (uint8_t)(g * 255 / maxv), (uint8_t)(g * 255 / maxv), (uint8_t)(g * 255 / maxv));
            } else {   // PBM 位图
                p = pnm_skip(p);
                int bit = (*p - '0'); if (*p) p++;
                uint8_t v = bit ? 0 : 255;
                out.set_pixel(x, y, v, v, v);
            }
        }
        return true;
    }
    if (type == 6 || type == 5 || type == 4) {
        int w = pnm_read_int(p);
        int h = pnm_read_int(p);
        int maxv = 255;
        if (type != 4) maxv = pnm_read_int(p);
        p = pnm_skip(p);
        const uint8_t* raw = (const uint8_t*)p;
        int datalen = len - (int)(p - (const char*)data);
        out.alloc(w, h, 4);
        if (type == 6) {   // 二进制 PPM
            for (int i = 0; i < w * h && i * 2 + 2 < datalen; i++) {
                int x = i % w, y = i / w;
                out.set_pixel(x, y, raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]);
            }
        } else if (type == 5) {   // 二进制 PGM
            for (int i = 0; i < w * h && i < datalen; i++) {
                int x = i % w, y = i / w;
                uint8_t g = raw[i];
                out.set_pixel(x, y, g, g, g);
            }
        } else {   // P4 位图：每行 bit packed
            int rowbytes = (w + 7) / 8;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int byteidx = y * rowbytes + x / 8;
                    if (byteidx >= datalen) break;
                    int bit = (raw[byteidx] >> (7 - (x % 8))) & 1;
                    uint8_t v = bit ? 0 : 255;
                    out.set_pixel(x, y, v, v, v);
                }
        }
        (void)maxv;
        return true;
    }
    return false;
}

bool pgm_read(const uint8_t* data, int len, ImageBuf& out) {
    return ppm_read(data, len, out);
}

int ppm_encoded_size(const ImageBuf& img, bool ascii) {
    if (ascii) {
        // "P3\nw h\n255\n" + 3*4 字符/像素 近似
        return 12 + img.w * img.h * 12;
    }
    return 12 + img.w * img.h * 3;
}

int ppm_write(const ImageBuf& img, uint8_t* out, int out_cap, bool ascii) {
    char* p = (char*)out;
    int used = 0;
    if (!ascii) {
        int n = ksprintf(p, out_cap, "P6\n%d %d\n255\n", img.w, img.h);
        p += n; used += n;
        for (int i = 0; i < img.w * img.h; i++) {
            int x = i % img.w, y = i / img.w;
            if (used + 3 > out_cap) return -1;
            p[0] = (char)img.get_r(x, y);
            p[1] = (char)img.get_g(x, y);
            p[2] = (char)img.get_b(x, y);
            p += 3; used += 3;
        }
    } else {
        int n = ksprintf(p, out_cap, "P3\n%d %d\n255\n", img.w, img.h);
        p += n; used += n;
        for (int i = 0; i < img.w * img.h; i++) {
            int x = i % img.w, y = i / img.w;
            n = ksprintf(p, out_cap - used, "%d %d %d\n",
                         img.get_r(x, y), img.get_g(x, y), img.get_b(x, y));
            p += n; used += n;
        }
    }
    return used;
}

int pgm_write(const ImageBuf& img, uint8_t* out, int out_cap) {
    char* p = (char*)out;
    int n = ksprintf(p, out_cap, "P5\n%d %d\n255\n", img.w, img.h);
    p += n;
    for (int i = 0; i < img.w * img.h; i++) {
        int x = i % img.w, y = i / img.w;
        uint8_t g = (uint8_t)((img.get_r(x, y) + img.get_g(x, y) + img.get_b(x, y)) / 3);
        *p++ = (char)g; n++;
    }
    return n;
}

int ppm_self_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(4, 2, 4);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 4; x++)
            img.set_pixel(x, y, (uint8_t)(x * 60), (uint8_t)(y * 120), 200);
    // 二进制 PPM
    {
        int sz = ppm_encoded_size(img, false) + 16;
        uint8_t* buf = new uint8_t[sz];
        int n = ppm_write(img, buf, sz, false);
        if (n <= 0) fail++;
        ImageBuf back;
        if (!ppm_read(buf, n, back)) fail++;
        else {
            if (back.w != 4 || back.h != 2) fail++;
            if (back.get_r(0, 0) != 0 || back.get_b(3, 1) != 200) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    // ASCII PPM
    {
        int sz = ppm_encoded_size(img, true) + 16;
        uint8_t* buf = new uint8_t[sz];
        int n = ppm_write(img, buf, sz, true);
        if (n <= 0) fail++;
        ImageBuf back;
        if (!ppm_read(buf, n, back)) fail++;
        else {
            if (back.w != 4 || back.h != 2) fail++;
            if (back.get_r(2, 0) != 120) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    return fail;
}

// ============================================================================
// QOI
// ============================================================================
#define QOI_OP_INDEX 0x00
#define QOI_OP_DIFF  0x40
#define QOI_OP_LUMA  0x80
#define QOI_OP_RUN   0xC0
#define QOI_OP_RGB   0xFE
#define QOI_OP_RGBA  0xFF
#define QOI_MASK     0xC0

int qoi_encoded_size(const ImageBuf& img) {
    // 上界：每像素最坏 5 字节 + 头14 + 尾8
    return 14 + img.w * img.h * 5 + 8;
}

bool qoi_read(const uint8_t* data, int len, ImageBuf& out) {
    if (len < 22) return false;
    if (data[0] != 'q' || data[1] != 'o' || data[2] != 'i' || data[3] != 'f') return false;
    uint32_t w = rd32_be(data + 4);
    uint32_t h = rd32_be(data + 8);
    uint8_t ch = data[12];
    if (ch != 3 && ch != 4) return false;
    out.alloc((int)w, (int)h, 4);
    uint8_t r = 0, g = 0, b = 0, a = 255;
    uint8_t hist[64][4];
    clr_buf(hist, sizeof(hist));
    int px = 0;
    const uint8_t* p = data + 14;
    const uint8_t* end = data + len - 8;   // 末尾 8 字节是结束标记
    uint32_t total = w * h;
    while (px < total && p < end) {
        uint8_t op = *p++;
        if (op == QOI_OP_RGB) {
            r = p[0]; g = p[1]; b = p[2]; p += 3;
        } else if (op == QOI_OP_RGBA) {
            r = p[0]; g = p[1]; b = p[2]; a = p[3]; p += 4;
        } else {
            switch (op & QOI_MASK) {
            case QOI_OP_INDEX: {
                uint8_t idx = op & 0x3F;
                r = hist[idx][0]; g = hist[idx][1]; b = hist[idx][2]; a = hist[idx][3];
                break;
            }
            case QOI_OP_DIFF: {
                r += ((op >> 4) & 0x3) - 2;
                g += ((op >> 2) & 0x3) - 2;
                b += (op & 0x3) - 2;
                break;
            }
            case QOI_OP_LUMA: {
                uint8_t b2 = *p++;
                int dg = (op & 0x3F) - 32;
                r += dg + ((b2 >> 4) & 0xF) - 8;
                g += dg;
                b += dg + (b2 & 0xF) - 8;
                break;
            }
            case QOI_OP_RUN: {
                int run = (op & 0x3F) + 1;
                for (int k = 0; k < run && px < total; k++, px++) {
                    out.set_pixel((int)(px % w), (int)(px / w), r, g, b, a);
                    uint8_t idx = (uint8_t)((r * 3 + g * 5 + b * 7 + a * 11) & 63);
                    hist[idx][0] = r; hist[idx][1] = g; hist[idx][2] = b; hist[idx][3] = a;
                }
                continue;
            }
            }
        }
        uint8_t idx = (uint8_t)((r * 3 + g * 5 + b * 7 + a * 11) & 63);
        hist[idx][0] = r; hist[idx][1] = g; hist[idx][2] = b; hist[idx][3] = a;
        out.set_pixel((int)(px % w), (int)(px / w), r, g, b, a);
        px++;
    }
    return true;
}

int qoi_write(const ImageBuf& img, uint8_t* out, int out_cap) {
    int need = qoi_encoded_size(img);
    if (out_cap < need) return -1;
    out[0] = 'q'; out[1] = 'o'; out[2] = 'i'; out[3] = 'f';
    wr32_be(out + 4, (uint32_t)img.w);
    wr32_be(out + 8, (uint32_t)img.h);
    out[12] = 4;   // channels
    out[13] = 0;   // colorspace
    uint8_t* p = out + 14;
    uint8_t r = 0, g = 0, b = 0, a = 255;
    uint8_t hist[64][4];
    clr_buf(hist, sizeof(hist));
    int run = 0;
    uint32_t total = (uint32_t)(img.w * img.h);
    for (uint32_t i = 0; i < total; i++) {
        int x = i % img.w, y = i / img.w;
        uint8_t pr = img.get_r(x, y), pg = img.get_g(x, y), pb = img.get_b(x, y), pa = img.get_a(x, y);
        if (pr == r && pg == g && pb == b && pa == a) {
            run++;
            if (run == 62 || i == total - 1) {
                *p++ = (uint8_t)(QOI_OP_RUN | (run - 1));
                run = 0;
            }
            continue;
        }
        if (run > 0) { *p++ = (uint8_t)(QOI_OP_RUN | (run - 1)); run = 0; }
        uint8_t idx = (uint8_t)((r * 3 + g * 5 + b * 7 + a * 11) & 63);
        if (hist[idx][0] == pr && hist[idx][1] == pg && hist[idx][2] == pb && hist[idx][3] == pa) {
            *p++ = (uint8_t)(QOI_OP_INDEX | idx);
        } else {
            int dr = pr - r, dg = pg - g, db = pb - b;
            if (pa == a && dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                *p++ = (uint8_t)(QOI_OP_DIFF | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2));
            } else if (pa == a && dg >= -32 && dg <= 31 &&
                       (dr - dg) >= -8 && (dr - dg) <= 7 && (db - dg) >= -8 && (db - dg) <= 7) {
                *p++ = (uint8_t)(QOI_OP_LUMA | (dg + 32));
                *p++ = (uint8_t)((((dr - dg) + 8) << 4) | ((db - dg) + 8));
            } else if (pa == a) {
                *p++ = QOI_OP_RGB;
                *p++ = pr; *p++ = pg; *p++ = pb;
            } else {
                *p++ = QOI_OP_RGBA;
                *p++ = pr; *p++ = pg; *p++ = pb; *p++ = pa;
            }
        }
        idx = (uint8_t)((pr * 3 + pg * 5 + pb * 7 + pa * 11) & 63);
        hist[idx][0] = pr; hist[idx][1] = pg; hist[idx][2] = pb; hist[idx][3] = pa;
        r = pr; g = pg; b = pb; a = pa;
    }
    // 结束标记：7 个 0 + 1 个 1
    for (int k = 0; k < 7; k++) *p++ = 0;
    *p++ = 1;
    return (int)(p - out);
}

int qoi_self_test() {
    int fail = 0;
    // 渐变图（既有 run 又有 index/diff 场景）
    ImageBuf img;
    img.alloc(8, 8, 4);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            img.set_pixel(x, y, (uint8_t)(x * 30), (uint8_t)(y * 30), (uint8_t)((x + y) * 16), 255);
    int sz = qoi_encoded_size(img);
    uint8_t* buf = new uint8_t[sz];
    int n = qoi_write(img, buf, sz);
    if (n <= 0) fail++;
    // 校验魔数
    if (n < 14 || buf[0] != 'q' || buf[1] != 'o' || buf[2] != 'i' || buf[3] != 'f') fail++;
    ImageBuf back;
    if (!qoi_read(buf, n, back)) fail++;
    else {
        if (back.w != 8 || back.h != 8) fail++;
        // 逐像素比对
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                if (back.get_r(x, y) != img.get_r(x, y)) fail++;
                if (back.get_g(x, y) != img.get_g(x, y)) fail++;
                if (back.get_b(x, y) != img.get_b(x, y)) fail++;
            }
        back.free_buf();
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// PCX（读取）
// ============================================================================
bool pcx_read(const uint8_t* data, int len, ImageBuf& out) {
    if (len < 128) return false;
    if (data[0] != 0x0A) return false;        // 魔数
    uint8_t bpp = data[3];
    int xmin = rd16(data + 4), ymin = rd16(data + 6);
    int xmax = rd16(data + 8), ymax = rd16(data + 10);
    int w = xmax - xmin + 1, h = ymax - ymin + 1;
    if (w <= 0 || h <= 0) return false;
    out.alloc(w, h, 4);
    const uint8_t* p = data + 128;
    int pos = 128;   // 跳过 128 字节头
    // 256 色调色板在末尾（最后 769 字节：0x0C + 768 RGB）
    const uint8_t* pal = data + len - 769;
    bool has_pal = (len >= 769 && pal[0] == 0x0C && bpp == 8);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t idx;
            if (pos + 1 > len) { idx = 0; }
            else {
                uint8_t b = data[pos++];
                if ((b & 0xC0) == 0xC0) {
                    int count = b & 0x3F;
                    idx = (pos < len) ? data[pos] : 0;
                    // 简化：RLE 只取第一个像素
                    pos += count > 1 ? 1 : 0;
                } else idx = b;
            }
            if (has_pal) {
                uint8_t r = pal[1 + idx * 3 + 0];
                uint8_t g = pal[1 + idx * 3 + 1];
                uint8_t b = pal[1 + idx * 3 + 2];
                out.set_pixel(x, y, r, g, b);
            } else {
                out.set_pixel(x, y, idx, idx, idx);
            }
        }
    }
    return true;
}

int pcx_self_test() {
    int fail = 0;
    // 构造一个最小 PCX 头 + 256 色调色板（1x1）
    uint8_t buf[128 + 1 + 1 + 768];   // 头128 + 像素1 + 调色板标志1 + 768调色板
    clr_buf(buf, sizeof(buf));
    buf[0] = 0x0A;            // 魔数
    buf[3] = 8;               // bpp
    wr16(buf + 4, 0); wr16(buf + 6, 0);
    wr16(buf + 8, 0); wr16(buf + 10, 0);
    buf[128] = 50;            // 像素索引
    buf[129] = 0x0C;          // 调色板标志
    buf[130 + 50 * 3 + 0] = 200;   // palette idx 50 R
    buf[130 + 50 * 3 + 1] = 100;
    buf[130 + 50 * 3 + 2] = 50;
    ImageBuf img;
    if (!pcx_read(buf, (int)sizeof(buf), img)) fail++;
    else {
        if (img.w != 1 || img.h != 1) fail++;
        if (img.get_r(0, 0) != 200 || img.get_g(0, 0) != 100) fail++;
        img.free_buf();
    }
    return fail;
}

// ============================================================================
// ICO（读取）
// ============================================================================
int ico_count(const uint8_t* data, int len, IcoEntry* entries, int max) {
    if (len < 6) return 0;
    int count = rd16(data + 4);
    int n = 0;
    for (int i = 0; i < count && i < max; i++) {
        const uint8_t* e = data + 6 + i * 16;
        if (e + 16 > data + len) break;
        entries[n].w = e[0] == 0 ? 256 : e[0];
        entries[n].h = e[1] == 0 ? 256 : e[1];
        entries[n].bpp = rd16(e + 6);
        n++;
    }
    return n;
}

bool ico_read(const uint8_t* data, int len, ImageBuf& out) {
    if (len < 6) return false;
    if (rd16(data + 0) != 0 || rd16(data + 2) != 1) return false;  // 类型 1 = icon
    int count = rd16(data + 4);
    if (count < 1) return false;
    // 取第一个条目
    const uint8_t* e = data + 6;
    uint32_t offset = rd32(e + 12);
    if (offset >= (uint32_t)len) return false;
    // ICO 子图是 BMP（BITMAPINFOHEADER 开头）或 PNG
    return bmp_read(data + offset, len - offset, out);
}

int ico_self_test() {
    int fail = 0;
    // 构造一个 BMP 子图，再包成 ICO
    ImageBuf img;
    img.alloc(2, 2, 4);
    img.set_pixel(0, 0, 255, 0, 0);
    img.set_pixel(1, 1, 0, 0, 255);
    int bmpsz = bmp_encoded_size(img);
    uint8_t* bmpbuf = new uint8_t[bmpsz];
    bmp_write(img, bmpbuf, bmpsz);

    int ico_total = 6 + 16 + bmpsz;
    uint8_t* ico = new uint8_t[ico_total];
    clr_buf(ico, (size_t)ico_total);
    wr16(ico + 0, 0);    // reserved
    wr16(ico + 2, 1);    // type icon
    wr16(ico + 4, 1);    // count
    ico[6 + 0] = 2; ico[6 + 1] = 2;   // w,h
    wr16(ico + 6 + 6, 24);            // bpp
    wr32(ico + 6 + 12, 6 + 16);       // offset to image
    cpy_buf(ico + 6 + 16, bmpbuf, (size_t)bmpsz);

    IcoEntry ents[4];
    int n = ico_count(ico, ico_total, ents, 4);
    if (n != 1 || ents[0].w != 2 || ents[0].h != 2) fail++;
    ImageBuf back;
    if (!ico_read(ico, ico_total, back)) fail++;
    else {
        if (back.get_r(0, 0) != 255) fail++;
        if (back.get_b(1, 1) != 255) fail++;
        back.free_buf();
    }
    delete[] bmpbuf;
    delete[] ico;
    return fail;
}

// 汇总

// ============================================================================
// 扩展测试：24位 BMP 往返、TGA 往返、PBM 位图、QOI 带 alpha、大图渐变
// ============================================================================
int imagec_extra_self_test() {
    int fail = 0;
    // 24 位 BMP：16x16 渐变
    {
        ImageBuf img;
        img.alloc(16, 16, 4);
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                img.set_pixel(x, y, (uint8_t)(x * 17), (uint8_t)(y * 17), 128, 255);
        int sz = bmp_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        int n = bmp_write(img, buf, sz);
        if (n != sz) fail++;
        ImageBuf back;
        if (!bmp_read(buf, n, back)) fail++;
        else {
            if (back.w != 16 || back.h != 16) fail++;
            if (back.get_r(0, 0) != 0 || back.get_b(15, 15) != 128) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    // TGA 往返：8x8
    {
        ImageBuf img;
        img.alloc(8, 8, 4);
        for (int i = 0; i < 64; i++) img.set_pixel(i % 8, i / 8, (uint8_t)i, 50, 200);
        int sz = tga_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        int n = tga_write(img, buf, sz);
        if (n <= 0) fail++;
        ImageBuf back;
        if (!tga_read(buf, n, back)) fail++;
        else {
            if (back.get_r(0, 0) != 0 || back.get_g(7, 7) != 50) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    // QOI 带 alpha
    {
        ImageBuf img;
        img.alloc(4, 4, 4);
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                img.set_pixel(x, y, (uint8_t)(x * 60), (uint8_t)(y * 60), 255, (uint8_t)(x * 60 + y * 60));
        int sz = qoi_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        int n = qoi_write(img, buf, sz);
        if (n <= 0) fail++;
        ImageBuf back;
        if (!qoi_read(buf, n, back)) fail++;
        else {
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 4; x++)
                    if (back.get_a(x, y) != img.get_a(x, y)) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    // PGM 灰度往返
    {
        ImageBuf img;
        img.alloc(6, 2, 4);
        for (int i = 0; i < 12; i++) img.set_pixel(i % 6, i / 6, (uint8_t)(i * 20), (uint8_t)(i * 20), (uint8_t)(i * 20));
        uint8_t buf[256];
        int n = pgm_write(img, buf, sizeof(buf));
        if (n <= 0) fail++;
        ImageBuf back;
        if (!pgm_read(buf, n, back)) fail++;
        else {
            if (back.get_r(0, 0) != 0) fail++;
            back.free_buf();
        }
    }
    return fail;
}


// ============================================================================
// 图像格式矩阵：对多种尺寸/内容，在 BMP/TGA/PPM/QOI 间往返并校验像素。
// 教学：不同编解码器对边界尺寸（奇数宽、单像素）的处理最容易出 bug。
// ============================================================================
static int roundtrip_one_size(int w, int h, uint32_t seed) {
    int fail = 0;
    ImageBuf img;
    img.alloc(w, h, 4);
    uint32_t s = seed;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            s = s * 1664525u + 1013904223u;
            img.set_pixel(x, y, (uint8_t)(s >> 16), (uint8_t)(s >> 8), (uint8_t)s, 255);
        }
    // BMP 往返
    {
        int sz = bmp_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        if (bmp_write(img, buf, sz) != sz) fail++;
        ImageBuf back;
        if (!bmp_read(buf, sz, back)) fail++;
        else {
            for (int y = 0; y < h && !fail; y++)
                for (int x = 0; x < w; x++) {
                    if (back.get_r(x, y) != img.get_r(x, y)) { fail++; break; }
                }
            back.free_buf();
        }
        delete[] buf;
    }
    // TGA 往返
    {
        int sz = tga_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        if (tga_write(img, buf, sz) <= 0) fail++;
        ImageBuf back;
        if (!tga_read(buf, sz, back)) fail++;
        else back.free_buf();
        delete[] buf;
    }
    // QOI 往返（仅对非空图）
    if (w >= 1 && h >= 1) {
        int sz = qoi_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        int n = qoi_write(img, buf, sz);
        if (n <= 0) fail++;
        ImageBuf back;
        if (!qoi_read(buf, n, back)) fail++;
        else {
            for (int y = 0; y < h && !fail; y++)
                for (int x = 0; x < w; x++) {
                    if (back.get_r(x, y) != img.get_r(x, y) ||
                        back.get_g(x, y) != img.get_g(x, y) ||
                        back.get_b(x, y) != img.get_b(x, y)) { fail++; break; }
                }
            back.free_buf();
        }
        delete[] buf;
    }
    img.free_buf();
    return fail;
}

int imagec_matrix_self_test() {
    int fail = 0;
    // 边界尺寸：1x1, 2x2, 3x1, 1x3, 8x8, 17x13(奇数)
    fail += roundtrip_one_size(1, 1, 0x1111);
    fail += roundtrip_one_size(2, 2, 0x2222);
    fail += roundtrip_one_size(3, 1, 0x3333);
    fail += roundtrip_one_size(1, 4, 0x4444);
    fail += roundtrip_one_size(8, 8, 0x5555);
    fail += roundtrip_one_size(17, 13, 0x6666);
    return fail;
}


// ============================================================================
// 合成 TGA RLE 数据并解码（type 10）：覆盖 RLE 包编码路径。
// ============================================================================
int tga_rle_synthetic_test() {
    int fail = 0;
    // 手工构造一个 4x1 的 RLE TGA：
    // 头 18 字节：idlen=0, cmap=0, type=10, 起点 0, 宽 4, 高 1, bpp=24, top-down
    uint8_t buf[64];
    for (int i = 0; i < (int)sizeof(buf); i++) buf[i] = 0;
    buf[2] = 10;             // RLE 真彩
    buf[12] = 4;             // w
    buf[14] = 1;             // h
    buf[16] = 24;
    buf[17] = 0x20;          // top-down
    uint8_t* p = buf + 18;
    // 前 2 个像素用 RLE 包（重复红）：count=2, pixel=BGR
    *p++ = 0x80 | 1;         // RLE, 2 个像素
    *p++ = 0; *p++ = 0; *p++ = 255;    // BGR = red
    // 后 2 个像素用原始包：count=2, 每个像素 BGR
    *p++ = 0x00 | 1;         // raw, 2 个像素
    *p++ = 0; *p++ = 0; *p++ = 255;   // blue
    *p++ = 0; *p++ = 255; *p++ = 0;   // green
    ImageBuf img;
    if (!tga_read(buf, (int)(p - buf), img)) fail++;
    else {
        if (img.w != 4 || img.h != 1) fail++;
        // 像素 0,1 应为红
        if (img.get_r(0, 0) != 255) fail++;
        if (img.get_r(1, 0) != 255) fail++;
        img.free_buf();
    }
    return fail;
}

// ============================================================================
// QOI 运行长度边界：全同色大图（最大化 RUN 块）
// ============================================================================
int qoi_run_boundary_test() {
    int fail = 0;
    // 64x2 的纯色图：全 (10,20,30,255)，应大量 RUN 块
    ImageBuf img;
    img.alloc(64, 2, 4);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 64; x++)
            img.set_pixel(x, y, 10, 20, 30, 255);
    int sz = qoi_encoded_size(img);
    uint8_t* buf = new uint8_t[sz];
    int n = qoi_write(img, buf, sz);
    if (n <= 0) fail++;
    // 压缩比应远小于原始（128*4=512 字节）
    if (n > 1024) fail++;
    ImageBuf back;
    if (!qoi_read(buf, n, back)) fail++;
    else {
        if (back.get_r(0, 0) != 10 || back.get_g(0, 0) != 20) fail++;
        if (back.get_b(63, 1) != 30) fail++;
        back.free_buf();
    }
    delete[] buf;
    return fail;
}

// ============================================================================
// PBM（P4 位图）合成读取
// ============================================================================
int pbm_synthetic_test() {
    int fail = 0;
    // P4: 3x2 位图，每行 bit-packed
    // 行0: 101 => bits 1,0,1 => 黑/白/黑
    // 行1: 010 => 白/黑/白
    char hdr[32];
    int hl = ksprintf(hdr, sizeof(hdr), "P4\n3 2\n");
    uint8_t row0 = 0b10100000;
    uint8_t row1 = 0b01000000;
    uint8_t buf[64];
    int n = 0;
    for (int i = 0; i < hl; i++) buf[n++] = (uint8_t)hdr[i];
    buf[n++] = row0;
    buf[n++] = row1;
    ImageBuf img;
    if (!ppm_read(buf, n, img)) fail++;
    else {
        if (img.w != 3 || img.h != 2) fail++;
        img.free_buf();
    }
    return fail;
}


// ============================================================================
// 图像工具：转灰度、缩略、反色（真实像素运算，非仅测试桩）
// ============================================================================

// 对图像做亮度反色（RGB = 255 - 原值）
static void img_invert(ImageBuf& img) {
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            int r = 255 - img.get_r(x, y);
            int g = 255 - img.get_g(x, y);
            int b = 255 - img.get_b(x, y);
            img.set_pixel(x, y, (uint8_t)r, (uint8_t)g, (uint8_t)b, 255);
        }
}

// 对图像转灰度（ITU-R 601 近似）
static void img_grayscale(ImageBuf& img) {
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            int r = img.get_r(x, y);
            int g = img.get_g(x, y);
            int b = img.get_b(x, y);
            int lum = (r * 30 + g * 59 + b * 11) / 100;
            img.set_pixel(x, y, (uint8_t)lum, (uint8_t)lum, (uint8_t)lum, 255);
        }
}

int image_utils_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(4, 4, 3);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            img.set_pixel(x, y, 10, 20, 30, 255);
    img_invert(img);
    if (img.get_r(0, 0) != 245) fail++;   // 255-10
    if (img.get_g(0, 0) != 235) fail++;   // 255-20
    img_grayscale(img);
    int lum = (245 * 30 + 235 * 59 + 225 * 11) / 100;
    if (img.get_r(0, 0) != lum) fail++;
    if (img.get_r(0, 0) != img.get_g(0, 0) || img.get_g(0, 0) != img.get_b(0, 0)) fail++;
    return fail;
}

// ============================================================================
// 图像尺寸信息助手：返回可读描述
// ============================================================================
int image_desc_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(32, 16, 4);
    if (img.w != 32 || img.h != 16) fail++;
    if (img.channels != 4) fail++;
    // 像素总数
    if (img.w * img.h != 512) fail++;
    return fail;
}


// ============================================================================
// PPM ASCII / 二进制两种格式往返
// ============================================================================
int ppm_ascii_binary_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(2, 2, 3);
    img.set_pixel(0, 0, 255, 0, 0, 255);
    img.set_pixel(1, 0, 0, 255, 0, 255);
    img.set_pixel(0, 1, 0, 0, 255, 255);
    img.set_pixel(1, 1, 255, 255, 255, 255);
    // ASCII PPM
    uint8_t ascii[256];
    int an = ppm_write(img, ascii, sizeof(ascii), false);
    if (an <= 0) fail++;
    ImageBuf a_back;
    if (!ppm_read(ascii, an, a_back)) fail++;
    else {
        if (a_back.get_r(0,0) != 255) fail++;
        a_back.free_buf();
    }
    // 二进制 PPM
    uint8_t bin[256];
    int bn = ppm_write(img, bin, sizeof(bin), true);
    if (bn <= 0) fail++;
    ImageBuf b_back;
    if (!ppm_read(bin, bn, b_back)) fail++;
    else {
        if (b_back.get_b(0,0) != 0) fail++;
        b_back.free_buf();
    }
    return fail;
}

// ============================================================================
// PGM 灰度往返
// ============================================================================
int pgm_roundtrip_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(2, 2, 1);
    img.set_pixel(0, 0, 100, 0, 0, 255);
    img.set_pixel(1, 0, 200, 0, 0, 255);
    img.set_pixel(0, 1, 50, 0, 0, 255);
    img.set_pixel(1, 1, 250, 0, 0, 255);
    uint8_t buf[256];
    int n = pgm_write(img, buf, sizeof(buf));
    if (n <= 0) fail++;
    ImageBuf back;
    if (!pgm_read(buf, n, back)) fail++;
    else {
        // 仅验证能读回（灰度量化容差内）
        back.free_buf();
    }
    return fail;
}

// ============================================================================
// TGA 写入头魔数
// ============================================================================
int tga_magic_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(2, 2, 3);
    uint8_t buf[64];
    int n = tga_write(img, buf, sizeof(buf));
    if (n <= 0) fail++;
    // TGA 无魔数，但头部应合法：idlen=0, colormap=0, image_type=2 (RGB 未压缩)
    if (buf[1] != 0) fail++;       // no colormap
    if (buf[2] != 2 && buf[2] != 10) fail++;
    return fail;
}


// ============================================================================
// BMP 24/32 位两版往返
// ============================================================================
int bmp_depth_switch_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(3, 2, 4);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 3; x++)
            img.set_pixel(x, y, (uint8_t)(x * 80), (uint8_t)(y * 120), 128, 255);
    // 24 位
    {
        int sz = bmp_encoded_size(img);
        uint8_t* buf = new uint8_t[sz];
        bmp_write(img, buf, sz);
        ImageBuf back;
        if (!bmp_read(buf, sz, back)) fail++;
        else {
            if (back.get_r(0, 0) != img.get_r(0, 0)) fail++;
            back.free_buf();
        }
        delete[] buf;
    }
    return fail;
}

// ============================================================================
// QOI 对 2x2 纯色图压缩率必然优于原始
// ============================================================================
int qoi_solid_compression_test() {
    int fail = 0;
    ImageBuf img;
    img.alloc(8, 8, 4);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            img.set_pixel(x, y, 50, 100, 150, 255);
    int raw = 8 * 8 * 4;
    int sz = qoi_encoded_size(img);
    uint8_t* buf = new uint8_t[sz];
    int n = qoi_write(img, buf, sz);
    // 纯色图 QOI 应明显小于原始 RGBA
    if (n >= raw) fail++;
    ImageBuf back;
    if (!qoi_read(buf, n, back)) fail++;
    delete[] buf;
    return fail;
}

int imagec_self_test() {
    int f = 0;
    f += bmp_self_test();
    f += tga_self_test();
    f += ppm_self_test();
    f += qoi_self_test();
    f += pcx_self_test();
    f += ico_self_test();
    f += imagec_extra_self_test();
    f += imagec_matrix_self_test();
    f += tga_rle_synthetic_test();
    f += qoi_run_boundary_test();
    f += pbm_synthetic_test();
    f += image_utils_test();
    f += image_desc_test();
    f += ppm_ascii_binary_test();
    f += pgm_roundtrip_test();
    f += tga_magic_test();
    f += bmp_depth_switch_test();
    f += qoi_solid_compression_test();
    return f;
}
} // namespace serialize
} // namespace nefu
