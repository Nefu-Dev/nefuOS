// nefuOS graphics library — color spaces & palettes (integer only)
// RGB888 <-> HSL/HSV/YUV/YCbCr/CMYK, gamma/linear, luminance, gradient
// and scientific palettes.  All arithmetic is integer (8-bit channels,
// H in 0..360, S/L/V in 0..100) so it runs anywhere.
#pragma once
#include <stdint.h>

namespace nefu {
namespace gfxlib {

struct RGB { int r, g, b; };          // 0..255
struct HSL { int h, s, l; };          // h 0..360, s/l 0..100
struct HSV { int h, s, v; };          // h 0..360, s/v 0..100
struct YUV { int y, u, v; };          // y 0..255, u/v -128..127
struct YCbCr { int y, cb, cr; };      // y 0..255, cb/cr 0..255
struct CMYK { int c, m, y, k; };      // 0..100 (percent)

RGB rgb(int r, int g, int b);
uint32_t rgb_pack(const RGB& c);                        // 0xFFRRGGBB
RGB rgb_unpack(uint32_t px);

// --- conversions ---
RGB hsl_to_rgb(const HSL& h);
HSL rgb_to_hsl(const RGB& c);
RGB hsv_to_rgb(const HSV& h);
HSV rgb_to_hsv(const RGB& c);
RGB yuv_to_rgb(const YUV& y);
YUV rgb_to_yuv(const RGB& c);
RGB ycbcr_to_rgb(const YCbCr& y);
YCbCr rgb_to_ycbcr(const RGB& c);
RGB cmyk_to_rgb(const CMYK& k);
CMYK rgb_to_cmyk(const RGB& c);

// --- analysis / adjustments ---
int rgb_luma(const RGB& c);                             // Rec.601 luma 0..255
RGB rgb_grayscale(const RGB& c);
RGB rgb_gamma(const RGB& c, int gamma_x100);            // gamma 0.1..5.0 (x100)
RGB rgb_brightness(const RGB& c, int delta);            // delta -255..255
RGB rgb_contrast(const RGB& c, int factor_x100);        // 0..300 (x100)

// --- palettes (t in 0..255) ---
RGB palette_rainbow(int t);                             // hue sweep
RGB palette_heatmap(int t);                             // black->red->yellow->white
RGB palette_plasma(int t);                              // purple-pink-orange sweep
RGB palette_grayscale(int t);
RGB lerp_rgb(const RGB& a, const RGB& b, int t);        // t 0..255

// --- self test ---
int color_self_test();

} // namespace gfxlib
} // namespace nefu
