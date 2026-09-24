// ============================================================================
// nefuOS 光线追踪引擎 —— rtfilter 实现（Q16.16 定点）
// ============================================================================
#include "rtfilter.h"

namespace nefu {
namespace raytrace {

static inline uint32_t px(const uint32_t* fb, int w, int h, int x, int y) {
    if (x < 0) x = 0; if (x >= w) x = w - 1;
    if (y < 0) y = 0; if (y >= h) y = h - 1;
    return fb[y * w + x];
}

static inline void setpx(uint32_t* fb, int w, int x, int y, uint32_t c) {
    fb[y * w + x] = c;
}

void filter_blur(uint32_t* fb, int w, int h) {
    uint32_t* tmp = new uint32_t[w * h];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    uint32_t p = px(fb, w, h, x+dx, y+dy);
                    r += (p >> 16) & 0xFF;
                    g += (p >> 8) & 0xFF;
                    b += p & 0xFF;
                }
            tmp[y * w + x] = ((r/9) << 16) | ((g/9) << 8) | (b/9);
        }
    }
    for (int i = 0; i < w * h; i++) fb[i] = tmp[i];
    delete[] tmp;
}

void filter_sharpen(uint32_t* fb, int w, int h, rtfx amount) {
    uint32_t* tmp = new uint32_t[w * h];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = px(fb, w, h, x, y);
            uint32_t n = px(fb, w, h, x, y-1);
            uint32_t s = px(fb, w, h, x, y+1);
            uint32_t e = px(fb, w, h, x+1, y);
            uint32_t wt = px(fb, w, h, x-1, y);
            int cr = (c >> 16) & 0xFF, cg = (c >> 8) & 0xFF, cb = c & 0xFF;
            int nr = (((n>>16)&0xFF)+((s>>16)&0xFF)+((e>>16)&0xFF)+((wt>>16)&0xFF))/4;
            int ng = (((n>>8)&0xFF)+((s>>8)&0xFF)+((e>>8)&0xFF)+((wt>>8)&0xFF))/4;
            int nb = (((n)&0xFF)+((s)&0xFF)+((e)&0xFF)+((wt)&0xFF))/4;
            rtfx a = amount;
            int r = cr + rt_fxtoi(rt_mul(a, rt_itofx(cr - nr)));
            int g = cg + rt_fxtoi(rt_mul(a, rt_itofx(cg - ng)));
            int b = cb + rt_fxtoi(rt_mul(a, rt_itofx(cb - nb)));
            if (r<0)r=0; if(r>255)r=255;
            if (g<0)g=0; if(g>255)g=255;
            if (b<0)b=0; if(b>255)b=255;
            tmp[y*w+x] = (r<<16)|(g<<8)|b;
        }
    }
    for (int i = 0; i < w*h; i++) fb[i] = tmp[i];
    delete[] tmp;
}

static const int BAYER[16] = {
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
};

void filter_dither(uint32_t* fb, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t p = fb[y*w+x];
            int bv = BAYER[(y%4)*4 + (x%4)];
            int add = bv * 16;   // 0..255
            int r = ((p>>16)&0xFF) + add;
            int g = ((p>>8)&0xFF) + add;
            int b = (p&0xFF) + add;
            if (r>255)r=255; if(g>255)g=255; if(b>255)b=255;
            fb[y*w+x] = (r<<16)|(g<<8)|b;
        }
    }
}

void filter_brightness(uint32_t* fb, int w, int h, rtfx bright) {
    for (int i = 0; i < w*h; i++) {
        uint32_t p = fb[i];
        int r = rt_fxtoi(rt_mul(rt_itofx((p>>16)&0xFF), bright));
        int g = rt_fxtoi(rt_mul(rt_itofx((p>>8)&0xFF), bright));
        int b = rt_fxtoi(rt_mul(rt_itofx(p&0xFF), bright));
        if (r>255)r=255; if(g>255)g=255; if(b>255)b=255;
        fb[i] = (r<<16)|(g<<8)|b;
    }
}

void filter_extract_luma(const uint32_t* fb, int w, int h, rtfx* out) {
    for (int i = 0; i < w*h; i++) {
        uint32_t p = fb[i];
        int r = (p>>16)&0xFF, g = (p>>8)&0xFF, b = p&0xFF;
        out[i] = rt_mul(rt_itofx(r), fx::fxf(3,10))
               + rt_mul(rt_itofx(g), fx::fxf(6,10))
               + rt_mul(rt_itofx(b), fx::fxf(1,10));
    }
}

void filter_deinterlace(uint32_t* fb, int w, int h) {
    for (int y = 1; y < h-1; y += 2) {
        for (int x = 0; x < w; x++) {
            uint32_t a = fb[(y-1)*w+x];
            uint32_t b = fb[(y+1)*w+x];
            int r = (((a>>16)&0xFF)+((b>>16)&0xFF))/2;
            int g = (((a>>8)&0xFF)+((b>>8)&0xFF))/2;
            int bl = (((a)&0xFF)+((b)&0xFF))/2;
            fb[y*w+x] = (r<<16)|(g<<8)|bl;
        }
    }
}

int rtfilter_self_test() {
    int fail = 0;
    // 1. 模糊后像素仍在范围内
    {
        uint32_t fb[4] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
        filter_blur(fb, 2, 2);
        for (int i = 0; i < 4; i++) if (fb[i] > 0xFFFFFF) fail++;
    }
    // 2. 亮度加倍
    {
        uint32_t fb[1] = {0x7F7F7F};
        filter_brightness(fb, 1, 1, rt_itofx(2));
        int r = (fb[0]>>16)&0xFF;
        if (r < 200) fail++;
    }
    // 3. 抖动不越界
    {
        uint32_t fb[4] = {0,0,0,0};
        filter_dither(fb, 2, 2);
        for (int i = 0; i < 4; i++) if (fb[i] > 0xFFFFFF) fail++;
    }
    // 4. 提取亮度
    {
        uint32_t fb[1] = {0xFF0000};
        rtfx l;
        filter_extract_luma(fb, 1, 1, &l);
        if (l <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
