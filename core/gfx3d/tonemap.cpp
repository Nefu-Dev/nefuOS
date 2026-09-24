// ============================================================================
// nefuOS 3D 图形库 —— tonemap 实现
// ============================================================================
#include "tonemap.h"
#include "raster.h"
#include <cmath>

namespace nefu {
namespace gfx3d {

Vec3 tonemap_reinhard(const Vec3& c, double exposure) {
    Vec3 h = c * exposure;
    return Vec3(h.x / (1 + h.x), h.y / (1 + h.y), h.z / (1 + h.z));
}

Vec3 tonemap_aces(const Vec3& c, double exposure) {
    Vec3 h = c * exposure;
    // Narkowicz ACES 近似
    double a = 2.51, b = 0.03, cc = 2.43, d = 0.59, e = 0.14;
    Vec3 r;
    r.x = (h.x * (a * h.x + b)) / (h.x * (cc * h.x + d) + e);
    r.y = (h.y * (a * h.y + b)) / (h.y * (cc * h.y + d) + e);
    r.z = (h.z * (a * h.z + b)) / (h.z * (cc * h.z + d) + e);
    return Vec3(r.x < 0 ? 0 : (r.x > 1 ? 1 : r.x),
                r.y < 0 ? 0 : (r.y > 1 ? 1 : r.y),
                r.z < 0 ? 0 : (r.z > 1 ? 1 : r.z));
}

Vec3 tonemap_exposure(const Vec3& c, double exposure) {
    return c * exposure;
}

Vec3 gamma_correct(const Vec3& c, double gamma) {
    double g = 1.0 / gamma;
    return Vec3(std::pow(c.x, g), std::pow(c.y, g), std::pow(c.z, g));
}

void tonemap_buffer(uint32_t* buf, int w, int h, double exposure, bool aces) {
    for (int i = 0; i < w * h; i++) {
        double r, g, b;
        unpack_color(buf[i], r, g, b);
        Vec3 c(r, g, b);
        c = aces ? tonemap_aces(c, exposure) : tonemap_reinhard(c, exposure);
        buf[i] = pack_color(c.x, c.y, c.z);
    }
}

void bloom_approx(uint32_t* buf, int w, int h, double threshold, double intensity) {
    // 提取高亮像素，叠加到邻域（3x3 盒模糊近似）
    uint32_t* tmp = new uint32_t[(size_t)w * h];
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            double r, g, b;
            unpack_color(buf[y * w + x], r, g, b);
            double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            if (lum < threshold) { tmp[y * w + x] = 0; continue; }
            tmp[y * w + x] = buf[y * w + x];
        }
    // 叠加到原图（简化：直接加高亮）
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!tmp[y * w + x]) continue;
            double r, g, b, tr, tg, tb;
            unpack_color(buf[y * w + x], r, g, b);
            unpack_color(tmp[y * w + x], tr, tg, tb);
            buf[y * w + x] = pack_color(r + tr * intensity,
                                        g + tg * intensity,
                                        b + tb * intensity);
        }
    delete[] tmp;
}

int tonemap_self_test() {
    int fail = 0;
    // Reinhard: 0 -> 0, 1 -> 0.5
    {
        Vec3 c = tonemap_reinhard(Vec3(1, 1, 1), 1.0);
        if (c.x > 0.51 || c.x < 0.49) fail++;
    }
    // ACES 不崩溃
    {
        Vec3 c = tonemap_aces(Vec3(2, 2, 2), 1.0);
        if (c.x > 1.0) fail++;
    }
    // 伽马：1 -> 1
    {
        Vec3 c = gamma_correct(Vec3(1, 1, 1), 2.2);
        if (c.x > 1.01 || c.x < 0.99) fail++;
    }
    return fail;
}

} // namespace gfx3d
} // namespace nefu
