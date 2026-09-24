// ============================================================================
// nefuOS 光线追踪引擎 —— rtutil 实现（Q16.16 定点）
// ============================================================================
#include "rtutil.h"
#include <stdio.h>

namespace nefu {
namespace raytrace {

void rt_write_pixel(uint32_t* fb, int x, int y, int w, int h, const RTVec3& color) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    rtfx rr = rt_div(color.x, RT_ONE + color.x);
    rtfx gg = rt_div(color.y, RT_ONE + color.y);
    rtfx bb = rt_div(color.z, RT_ONE + color.z);
    rr = rt_sqrt(rr); gg = rt_sqrt(gg); bb = rt_sqrt(bb);
    int R = rt_fxtoi(rr * rt_itofx(255));
    int G = rt_fxtoi(gg * rt_itofx(255));
    int B = rt_fxtoi(bb * rt_itofx(255));
    if (R < 0) R = 0; if (R > 255) R = 255;
    if (G < 0) G = 0; if (G > 255) G = 255;
    if (B < 0) B = 0; if (B > 255) B = 255;
    fb[y * w + x] = ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}

void rt_blit_upscale(const uint32_t* src, int sw, int sh,
                     uint32_t* dst, int dw, int dh, int scale) {
    for (int y = 0; y < dh; y++) {
        int sy = y / scale;
        if (sy >= sh) sy = sh - 1;
        for (int x = 0; x < dw; x++) {
            int sx = x / scale;
            if (sx >= sw) sx = sw - 1;
            dst[y * dw + x] = src[sy * sw + sx];
        }
    }
}

RTVec3 rt_adjust_brightness(const RTVec3& c, rtfx bright) {
    return RTVec3(rt_mul(c.x, bright), rt_mul(c.y, bright), rt_mul(c.z, bright));
}

RTVec3 rt_adjust_contrast(const RTVec3& c, rtfx contrast) {
    rtfx mid = RT_HALF;
    return RTVec3(rt_mul(c.x - mid, contrast) + mid,
                  rt_mul(c.y - mid, contrast) + mid,
                  rt_mul(c.z - mid, contrast) + mid);
}

int rt_write_ppm(const char* path, const uint32_t* fb, int w, int h) {
    FILE* fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "P3\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint32_t p = fb[i];
        fprintf(fp, "%d %d %d\n", (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    }
    fclose(fp);
    return w * h;
}

rtfx rt_frame_avg_luma(const uint32_t* fb, int w, int h) {
    rtfx sum = 0;
    int n = w * h;
    if (n <= 0) return 0;
    for (int i = 0; i < n; i++) {
        uint32_t p = fb[i];
        int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
        // 亮度 0.299r + 0.587g + 0.114b（近似定点）
        rtfx l = rt_mul(rt_itofx(r), fx::fxf(3,10))
               + rt_mul(rt_itofx(g), fx::fxf(6,10))
               + rt_mul(rt_itofx(b), fx::fxf(1,10));
        sum += l;
    }
    return rt_div(sum, rt_itofx(n));
}

int rtutil_self_test() {
    int fail = 0;
    // 1. 写像素再读回
    {
        uint32_t fb[4] = {0};
        rt_write_pixel(fb, 0, 0, 2, 2, RTVec3(RT_ONE, RT_ONE, RT_ONE));
        if ((fb[0] & 0xFF) < 200) fail++;   // 白像素 B 通道应接近 255
    }
    // 2. 放大 blit：1x1 白 -> 4x4 应全白
    {
        uint32_t src[1] = {0xFFFFFF};
        uint32_t dst[16] = {0};
        rt_blit_upscale(src, 1, 1, dst, 4, 4, 4);
        if (dst[15] != 0xFFFFFF) fail++;
    }
    // 3. 亮度调整
    {
        RTVec3 c = rt_adjust_brightness(RTVec3(RT_HALF,RT_HALF,RT_HALF), rt_itofx(2));
        if (!rt_near(c.x, RT_ONE, 300)) fail++;
    }
    // 4. 平均亮度
    {
        uint32_t fb[4] = {0xFFFFFF, 0xFFFFFF, 0x000000, 0x000000};
        rtfx l = rt_frame_avg_luma(fb, 2, 2);
        if (l <= 0) fail++;
    }
    return fail;
}

} // namespace raytrace
} // namespace nefu
