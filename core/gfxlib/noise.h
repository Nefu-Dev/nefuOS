// nefuOS graphics library — noise & fractals (integer / Q16.16 only)
// Hash-based value noise, Perlin gradient noise, fractal Brownian motion,
// Worley cell noise, Mandelbrot/Julia escape-time (Q16.16), L-Systems,
// iterated function systems and the logistic map.  No FPU needed.
#pragma once
#include <stdint.h>
#include "../lib/softmath.h"

namespace nefu {
namespace gfxlib {

// --- integer hashes ---
uint32_t hash_u32(uint32_t x);                    // Wang hash (good avalanche)
uint32_t hash2(int x, int y, uint32_t seed);      // 2D integer hash
uint32_t hash3(int x, int y, int z, uint32_t seed);

// --- value noise (Q16.16 output in [0, 65536)) ---
nefu::fx::fix value_noise2d(int x, int y, uint32_t seed);        // lattice value
nefu::fx::fix value_noise2d_smooth(nefu::fx::fix x, nefu::fx::fix y, uint32_t seed);
// --- Perlin gradient noise (Q16.16, roughly [-1,1]) ---
nefu::fx::fix perlin2d(nefu::fx::fix x, nefu::fx::fix y, uint32_t seed);
// --- fractal Brownian motion (octaves of perlin2d) ---
nefu::fx::fix fbm2d(nefu::fx::fix x, nefu::fx::fix y, int octaves,
                    nefu::fx::fix lacunarity, nefu::fx::fix gain, uint32_t seed);
// --- Worley cell noise: returns distance to nearest feature point, Q16.16 ---
nefu::fx::fix worley2d(nefu::fx::fix x, nefu::fx::fix y, uint32_t seed);

// --- fractals ---
// Mandelbrot: iterations until |z| exceeds 2 (Q16.16 complex arithmetic).
// Returns -1 when the point is inside the set after max_iter.
int mandelbrot_escape(nefu::fx::fix cx, nefu::fx::fix cy, int max_iter);
// Julia set for parameter c; same contract.
int julia_escape(nefu::fx::fix zx, nefu::fx::fix zy,
                 nefu::fx::fix cx, nefu::fx::fix cy, int max_iter);

// --- L-system ---
// A minimal string-rewriting system for fractal trees / curves.
struct LSystem {
    char axiom[64];
    char ruleA[64];       // replacement for 'A' ("" = keep)
    char ruleB[64];       // replacement for 'B'
    nefu::fx::fix angle;  // turtle turn angle (Q16.16 radians)
    nefu::fx::fix step;   // forward step length (Q16.16)
    char output[4096];
    int out_len;
    // expand the axiom 'depth' times into output
    void expand(int depth);
};

// --- iterated function system ---
// classic barnsley fern coefficients; apply one random rule (Q16.16)
void ifs_barnsley(nefu::fx::fix rnd, nefu::fx::fix* x, nefu::fx::fix* y);

// --- logistic map: deterministic chaotic sequence in [0,1) ---
// x_{n+1} = r * x_n * (1 - x_n); r is Q16.16 (3.57..4 typical)
nefu::fx::fix logistic_map(nefu::fx::fix x, nefu::fx::fix r);

// --- self test ---
int noise_self_test();

} // namespace gfxlib
} // namespace nefu
