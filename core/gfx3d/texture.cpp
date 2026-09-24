// ============================================================================
// nefuOS 3D 图形库 —— texture 实现
// ============================================================================
#include "texture.h"
#include <cmath>
#include <cstdlib>

namespace nefu {
namespace gfx3d {

void Texture::alloc(int W, int H) {
    free();
    w = W; h = H;
    pixels = new uint32_t[(size_t)w * h];
}

void Texture::free() {
    if (pixels) { delete[] pixels; pixels = 0; }
}

void Texture::fill(uint32_t c) {
    for (int i = 0; i < w * h; i++) pixels[i] = c;
}

void Texture::make_checkerboard(int size, uint32_t c0, uint32_t c1) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int cx = x / size, cy = y / size;
            pixels[y * w + x] = ((cx + cy) & 1) ? c1 : c0;
        }
}

void Texture::make_gradient(uint32_t top, uint32_t bottom) {
    for (int y = 0; y < h; y++) {
        double t = (double)y / h;
        double r0, g0, b0, r1, g1, b1;
        unpack_color(top, r0, g0, b0);
        unpack_color(bottom, r1, g1, b1);
        uint32_t c = pack_color(r0*(1-t)+r1*t, g0*(1-t)+g1*t, b0*(1-t)+b1*t);
        for (int x = 0; x < w; x++) pixels[y * w + x] = c;
    }
}

void Texture::make_noise() {
    for (int i = 0; i < w * h; i++) {
        uint8_t v = (uint8_t)(rand() & 0xFF);
        pixels[i] = 0xFF000000 | (v << 16) | (v << 8) | v;
    }
}

uint32_t Texture::sample_nearest(double u, double v) const {
    int x = (int)(u * w) % w;
    int y = (int)(v * h) % h;
    if (x < 0) x += w; if (y < 0) y += h;
    return pixels[y * w + x];
}

uint32_t Texture::sample_bilinear(double u, double v) const {
    double fx = u * (w - 1), fy = v * (h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;
    double tx = fx - x0, ty = fy - y0;
    uint32_t c00 = pixels[y0 * w + x0];
    uint32_t c10 = pixels[y0 * w + x1];
    uint32_t c01 = pixels[y1 * w + x0];
    uint32_t c11 = pixels[y1 * w + x1];
    double r0,g0,b0, r1,g1,b1, r2,g2,b2, r3,g3,b3;
    unpack_color(c00, r0, g0, b0);
    unpack_color(c10, r1, g1, b1);
    unpack_color(c01, r2, g2, b2);
    unpack_color(c11, r3, g3, b3);
    double r = r0*(1-tx)*(1-ty) + r1*tx*(1-ty) + r2*(1-tx)*ty + r3*tx*ty;
    double g = g0*(1-tx)*(1-ty) + g1*tx*(1-ty) + g2*(1-tx)*ty + g3*tx*ty;
    double b = b0*(1-tx)*(1-ty) + b1*tx*(1-ty) + b2*(1-tx)*ty + b3*tx*ty;
    return pack_color(r, g, b);
}

int texture_self_test() {
    int fail = 0;
    Texture t;
    t.alloc(8, 8);
    t.make_checkerboard(2, 0xFF000000, 0xFFFFFFFF);
    if ((t.pixels[0] & 0xFFFFFF) != 0x000000) fail++;
    if ((t.pixels[2] & 0xFFFFFF) != 0xFFFFFF) fail++;
    uint32_t c = t.sample_nearest(0.5, 0.5);
    if ((c & 0xFFFFFF) == 0) fail++;
    t.free();
    return fail;
}

} // namespace gfx3d
} // namespace nefu
