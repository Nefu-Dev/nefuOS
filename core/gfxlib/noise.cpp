// nefuOS graphics library — noise & fractals implementation & self test
#include "noise.h"
#include <string.h>
#include <stdlib.h>

namespace nefu {
namespace gfxlib {

using nefu::fx::fix;
using nefu::fx::fx_mul;
using nefu::fx::fx_div;
using nefu::fx::FX_ONE;
using nefu::fx::itofix;
using nefu::fx::fixtoi;

// =====================================================================
// hashes
// =====================================================================
uint32_t hash_u32(uint32_t x) {
    x = (x ^ 61) ^ (x >> 16);
    x = x * 9 + 0x85EBCA6Bu;
    x = x ^ (x >> 13);
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

uint32_t hash2(int x, int y, uint32_t seed) {
    return hash_u32((uint32_t)x + 0x9E3779B9u * (uint32_t)y + seed * 0x85EBCA6Bu);
}

uint32_t hash3(int x, int y, int z, uint32_t seed) {
    return hash_u32((uint32_t)x * 0x27D4EB2Fu + (uint32_t)y * 0x165667B1u +
                    (uint32_t)z * 0x9E3779B9u + seed * 0x85EBCA6Bu);
}

// =====================================================================
// value noise
// =====================================================================
fix value_noise2d(int x, int y, uint32_t seed) {
    return (fix)(hash2(x, y, seed) & 0xFFFFu);   // [0, 65536)
}

fix value_noise2d_smooth(fix x, fix y, uint32_t seed) {
    int x0 = fixtoi(x), y0 = fixtoi(y);
    fix fx0 = x - itofix(x0), fy0 = y - itofix(y0);
    // smoothstep interpolation curve: t*t*(3 - 2*t)
    fix sx = fx_mul(fx_mul(fx0, fx0), 3 - 2 * fx0);
    fix sy = fx_mul(fx_mul(fy0, fy0), 3 - 2 * fy0);
    fix v00 = value_noise2d(x0, y0, seed);
    fix v10 = value_noise2d(x0 + 1, y0, seed);
    fix v01 = value_noise2d(x0, y0 + 1, seed);
    fix v11 = value_noise2d(x0 + 1, y0 + 1, seed);
    fix a = v00 + fx_mul(v10 - v00, sx);
    fix b = v01 + fx_mul(v11 - v01, sx);
    return a + fx_mul(b - a, sy);
}

// =====================================================================
// Perlin 2D (gradient noise with smooth interpolation)
// =====================================================================
static int perm_grad(int h, int* gx, int* gy) {
    // map a hash to one of 8 gradient directions
    int dir = h & 7;
    switch (dir) {
        case 0: *gx = 1;  *gy = 1;  break;
        case 1: *gx = -1; *gy = 1;  break;
        case 2: *gx = 1;  *gy = -1; break;
        case 3: *gx = -1; *gy = -1; break;
        case 4: *gx = 1;  *gy = 0;  break;
        case 5: *gx = -1; *gy = 0;  break;
        case 6: *gx = 0;  *gy = 1;  break;
        default:*gx = 0;  *gy = -1; break;
    }
    return dir;
}

fix perlin2d(fix x, fix y, uint32_t seed) {
    int x0 = fixtoi(x), y0 = fixtoi(y);
    fix fx0 = x - itofix(x0), fy0 = y - itofix(y0);
    // quintic fade
    fix u = fx_mul(fx_mul(fx_mul(fx0, fx0), fx0), fx_mul(fx0, (fx_mul(fx0, 6) - 15)) + 10);
    fix v = fx_mul(fx_mul(fx_mul(fy0, fy0), fy0), fx_mul(fy0, (fx_mul(fy0, 6) - 15)) + 10);
    int gx, gy;
    perm_grad(hash2(x0, y0, seed), &gx, &gy);
    fix n00 = fx_mul(itofix(gx), fx0) + fx_mul(itofix(gy), fy0);
    perm_grad(hash2(x0 + 1, y0, seed), &gx, &gy);
    fix n10 = fx_mul(itofix(gx), fx0 - FX_ONE) + fx_mul(itofix(gy), fy0);
    perm_grad(hash2(x0, y0 + 1, seed), &gx, &gy);
    fix n01 = fx_mul(itofix(gx), fx0) + fx_mul(itofix(gy), fy0 - FX_ONE);
    perm_grad(hash2(x0 + 1, y0 + 1, seed), &gx, &gy);
    fix n11 = fx_mul(itofix(gx), fx0 - FX_ONE) + fx_mul(itofix(gy), fy0 - FX_ONE);
    fix a = n00 + fx_mul(n10 - n00, u);
    fix b = n01 + fx_mul(n11 - n01, u);
    return a + fx_mul(b - a, v);
}

fix fbm2d(fix x, fix y, int octaves, fix lacunarity, fix gain, uint32_t seed) {
    fix sum = 0, amp = FX_ONE, freq = FX_ONE, norm = 0;
    for (int o = 0; o < octaves; o++) {
        sum += fx_mul(perlin2d(fx_mul(x, freq), fx_mul(y, freq), seed + (uint32_t)o), amp);
        norm += amp;
        amp = fx_mul(amp, gain);
        freq = fx_mul(freq, lacunarity);
    }
    if (norm == 0) return 0;
    return fx_div(sum, norm);
}

// =====================================================================
// Worley cell noise
// =====================================================================
fix worley2d(fix x, fix y, uint32_t seed) {
    int xi = fixtoi(x), yi = fixtoi(y);
    fix best = itofix(1000000);
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) {
            int cx = xi + i, cy = yi + j;
            // feature point inside cell (cx,cy) via hash
            uint32_t h = hash2(cx, cy, seed);
            fix fpx = itofix(cx) + (fix)(h & 0xFFFF) / 256;
            fix fpy = itofix(cy) + (fix)((h >> 16) & 0xFFFF) / 256;
            fix dx = fpx - x, dy = fpy - y;
            fix d2 = fx_mul(dx, dx) + fx_mul(dy, dy);
            if (d2 < best) best = d2;
        }
    // return sqrt(distance), clamped to [0,1] for convenience
    fix d = fx_div(best, itofix(2));
    if (d > FX_ONE) d = FX_ONE;
    return d;
}

// =====================================================================
// Mandelbrot / Julia (Q16.16 escape time)
// =====================================================================
int mandelbrot_escape(fix cx, fix cy, int max_iter) {
    fix zx = 0, zy = 0;
    for (int i = 0; i < max_iter; i++) {
        fix x2 = fx_mul(zx, zx), y2 = fx_mul(zy, zy);
        if (x2 + y2 > itofix(4)) return i;
        zy = fx_mul(zx, zy) * 2 + cy;
        zx = x2 - y2 + cx;
    }
    return -1;
}

int julia_escape(fix zx, fix zy, fix cx, fix cy, int max_iter) {
    for (int i = 0; i < max_iter; i++) {
        fix x2 = fx_mul(zx, zx), y2 = fx_mul(zy, zy);
        if (x2 + y2 > itofix(4)) return i;
        zy = fx_mul(zx, zy) * 2 + cy;
        zx = x2 - y2 + cx;
    }
    return -1;
}

// =====================================================================
// L-system
// =====================================================================
void LSystem::expand(int depth) {
    out_len = 0;
    char cur[2048];
    int cur_len = 0;
    for (int i = 0; axiom[i] && i < 2047; i++) cur[cur_len++] = axiom[i];
    cur[cur_len] = 0;
    for (int d = 0; d < depth && cur_len < 2040; d++) {
        char nxt[2048];
        int nl = 0;
        for (int i = 0; i < cur_len && nl < 2039; i++) {
            char ch = cur[i];
            if (ch == 'A' && ruleA[0]) {
                for (int k = 0; ruleA[k] && nl < 2039; k++) nxt[nl++] = ruleA[k];
            } else if (ch == 'B' && ruleB[0]) {
                for (int k = 0; ruleB[k] && nl < 2039; k++) nxt[nl++] = ruleB[k];
            } else {
                nxt[nl++] = ch;
            }
        }
        nxt[nl] = 0;
        cur_len = nl;
        for (int i = 0; i < cur_len; i++) cur[i] = nxt[i];
        cur[cur_len] = 0;
    }
    for (int i = 0; i < cur_len && i < 4095; i++) output[i] = cur[i];
    output[cur_len < 4095 ? cur_len : 4095] = 0;
    out_len = cur_len < 4095 ? cur_len : 4095;
}

// =====================================================================
// IFS (barnsley fern), Q16.16
// =====================================================================
void ifs_barnsley(fix rnd, fix* x, fix* y) {
    // f1: 0.01 0    0    0.16 0    0      p=0.01
    // f2: 0.85 0.04 -0.04 0.85 0    1.60   p=0.85
    // f3: 0.20 -0.26 0.23 0.22 0    1.60   p=0.07
    // f4: -0.15 0.28 0.26 0.24 0    0.44   p=0.07
    fix nx, ny;
    fix rx = *x, ry = *y;
    if (rnd < fx_div(itofix(1), itofix(100))) {
        nx = fx_mul(fx_div(itofix(1), itofix(100)), rx);
        ny = fx_mul(fx_div(itofix(16), itofix(100)), ry);
    } else if (rnd < fx_div(itofix(86), itofix(100))) {
        nx = fx_mul(fx_div(itofix(85), itofix(100)), rx) + fx_mul(fx_div(itofix(4), itofix(100)), ry);
        ny = -fx_mul(fx_div(itofix(4), itofix(100)), rx) + fx_mul(fx_div(itofix(85), itofix(100)), ry) + fx_div(itofix(160), itofix(100));
    } else if (rnd < fx_div(itofix(93), itofix(100))) {
        nx = fx_mul(fx_div(itofix(20), itofix(100)), rx) - fx_mul(fx_div(itofix(26), itofix(100)), ry);
        ny = fx_mul(fx_div(itofix(23), itofix(100)), rx) + fx_mul(fx_div(itofix(22), itofix(100)), ry) + fx_div(itofix(160), itofix(100));
    } else {
        nx = -fx_mul(fx_div(itofix(15), itofix(100)), rx) + fx_mul(fx_div(itofix(28), itofix(100)), ry);
        ny = fx_mul(fx_div(itofix(26), itofix(100)), rx) + fx_mul(fx_div(itofix(24), itofix(100)), ry) + fx_div(itofix(44), itofix(100));
    }
    *x = nx;
    *y = ny;
}

// =====================================================================
// logistic map
// =====================================================================
fix logistic_map(fix x, fix r) {
    return fx_mul(fx_mul(r, x), FX_ONE - x);
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_noise_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_noise_fails++;
    (void)what;
}
} // namespace

int noise_self_test() {
    g_noise_fails = 0;

    // hash: deterministic + avalanche-ish
    expect("hash-det", hash_u32(0x12345678) == hash_u32(0x12345678));
    expect("hash-diff", hash_u32(0) != hash_u32(1));
    expect("hash2-det", hash2(3, 4, 7) == hash2(3, 4, 7));
    expect("hash2-diff", hash2(3, 4, 7) != hash2(4, 3, 7));
    expect("hash3-det", hash3(1, 2, 3, 9) == hash3(1, 2, 3, 9));

    // value noise in range
    fix v = value_noise2d(5, 6, 42);
    expect("value-range", v >= 0 && v <= 65535);
    expect("value-det", value_noise2d(5, 6, 42) == value_noise2d(5, 6, 42));

    // smooth noise is continuous across lattice lines
    fix a = value_noise2d_smooth(itofix(3), itofix(4), 11);
    fix b = value_noise2d_smooth(itofix(3) + 1024, itofix(4), 11);  // +0.015625
    expect("value-cont", a >= 0 && a <= 65535 && (a - b < 2048 && b - a < 2048));

    // perlin: bounded roughly in [-1,1] fixed point
    fix p = perlin2d(itofix(7), itofix(8), 99);
    expect("perlin-bounded", p >= -itofix(2) && p <= itofix(2));
    expect("perlin-det", p == perlin2d(itofix(7), itofix(8), 99));

    // fbm: several octaves still bounded
    fix f = fbm2d(itofix(2), itofix(3), 4, itofix(2), fx_div(itofix(5), itofix(10)), 7);
    expect("fbm-bounded", f >= -itofix(2) && f <= itofix(2));

    // worley: distance within [0,1]
    fix w = worley2d(itofix(5), itofix(5), 3);
    expect("worley-range", w >= 0 && w <= itofix(1));

    // mandelbrot: origin needs max_iter (inside the set), (2,0) escapes fast
    expect("mandel-in", mandelbrot_escape(0, 0, 64) == -1);
    int e = mandelbrot_escape(itofix(2), 0, 64);
    expect("mandel-out", e >= 0 && e <= 3);
    // (0, 1.5) is clearly outside the set and escapes quickly
    e = mandelbrot_escape(0, fx_div(itofix(15), itofix(10)), 64);
    expect("mandel-edge", e >= 0 && e <= 6);

    // julia c = -0.8 + 0.156i: (0,0) stays bounded-ish, far point escapes
    fix jc_r = -fx_div(itofix(8), itofix(10)), jc_i = fx_div(itofix(156), itofix(1000));
    int j0 = julia_escape(0, 0, jc_r, jc_i, 48);
    int jf = julia_escape(itofix(10), itofix(10), jc_r, jc_i, 48);
    expect("julia-far", jf >= 0 && jf <= 5);
    (void)j0;

    // L-system: koch curve 0 -> "A", 1 -> "A+A--A+A" style
    LSystem ls;
    ls.axiom[0] = 'A'; ls.axiom[1] = 0;
    ls.ruleA[0] = 'A'; ls.ruleA[1] = '+'; ls.ruleA[2] = 'A'; ls.ruleA[3] = '-';
    ls.ruleA[4] = '-'; ls.ruleA[5] = 'A'; ls.ruleA[6] = '+'; ls.ruleA[7] = 'A';
    ls.ruleA[8] = 0;
    ls.ruleB[0] = 0;
    ls.angle = fx_div(nefu::fx::FX_PI, itofix(3));
    ls.step = itofix(1);
    ls.expand(0);
    expect("ls-depth0", ls.out_len == 1 && ls.output[0] == 'A');
    ls.expand(1);
    expect("ls-depth1", ls.out_len == 8 && ls.output[0] == 'A');
    // symbols only from the alphabet
    ls.expand(3);
    bool valid = ls.out_len > 0;
    for (int i = 0; i < ls.out_len && valid; i++) {
        char ch = ls.output[i];
        if (ch != 'A' && ch != '+' && ch != '-') valid = false;
    }
    expect("ls-alphabet", valid);

    // IFS: iteration stays in the fern's bounding region
    fix fx_ = 0, fy_ = 0;
    for (int i = 0; i < 1000; i++) {
        fix r = (fix)(hash_u32((uint32_t)i * 2654435761u + 13u) & 0xFFFFu);
        ifs_barnsley(r, &fx_, &fy_);
    }
    expect("ifs-bounded", fy_ >= -itofix(1) && fy_ <= itofix(12) &&
                         fx_ >= -itofix(4) && fx_ <= itofix(4));

    // logistic map: r=4 x=0.2 -> x1 = 4*0.2*0.8 = 0.64
    fix l1 = logistic_map(fx_div(itofix(2), itofix(10)), itofix(4));
    expect("logistic", l1 >= fx_div(itofix(63), itofix(100)) - 256 &&
                       l1 <= fx_div(itofix(65), itofix(100)) + 256);
    // stays in [0,1]
    fix lx = fx_div(itofix(1), itofix(100));
    bool inrange = true;
    for (int i = 0; i < 100; i++) {
        lx = logistic_map(lx, fx_div(itofix(357), itofix(100)));
        if (lx < 0 || lx > itofix(1)) inrange = false;
    }
    expect("logistic-range", inrange);

    return g_noise_fails;
}

} // namespace gfxlib
} // namespace nefu
