// nefuOS graphics library — color implementation & self test
#include "color.h"

namespace nefu {
namespace gfxlib {

RGB rgb(int r, int g, int b) {
    RGB c;
    c.r = r < 0 ? 0 : (r > 255 ? 255 : r);
    c.g = g < 0 ? 0 : (g > 255 ? 255 : g);
    c.b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return c;
}

uint32_t rgb_pack(const RGB& c) {
    return 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

RGB rgb_unpack(uint32_t px) {
    RGB c;
    c.r = (int)((px >> 16) & 0xFF);
    c.g = (int)((px >> 8) & 0xFF);
    c.b = (int)(px & 0xFF);
    return c;
}

// =====================================================================
// HSL / HSV
// =====================================================================
static int hue_to_rgb(int p, int q, int t) {
    if (t < 0) t += 360;
    if (t >= 360) t -= 360;
    if (t < 60) return p + (q - p) * t / 60;
    if (t < 180) return q;
    if (t < 240) return p + (q - p) * (240 - t) / 60;
    return p;
}

RGB hsl_to_rgb(const HSL& h) {
    RGB c = {0, 0, 0};
    if (h.s == 0) { c.r = c.g = c.b = h.l * 255 / 100; return c; }
    int q = (h.l < 50) ? h.l * (100 + h.s) / 100 : (h.l + h.s - h.l * h.s / 100);
    int p = 2 * h.l - q;
    c.r = hue_to_rgb(p, q, h.h + 120);
    c.g = hue_to_rgb(p, q, h.h);
    c.b = hue_to_rgb(p, q, h.h - 120);
    // scale q,p from 0..100 to 0..255
    c.r = c.r * 255 / 100;
    c.g = c.g * 255 / 100;
    c.b = c.b * 255 / 100;
    return c;
}

HSL rgb_to_hsl(const RGB& c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    int d = mx - mn;
    HSL h;
    int l255 = (mx + mn) / 2;
    h.l = (l255 * 100 + 127) / 255;    // rounded to 0..100
    if (d == 0) { h.h = 0; h.s = 0; return h; }
    int den = 255 - (l255 < 128 ? 2 * l255 : 2 * (255 - l255));
    h.s = den > 0 ? d * 100 / den : 100;
    if (h.s > 100) h.s = 100;
    if (mx == c.r)      h.h = 60 * (c.g - c.b) / d + (c.g < c.b ? 360 : 0);
    else if (mx == c.g) h.h = 60 * (c.b - c.r) / d + 120;
    else                h.h = 60 * (c.r - c.g) / d + 240;
    if (h.h >= 360) h.h -= 360;
    return h;
}

RGB hsv_to_rgb(const HSV& h) {
    int s = h.s, v = h.v;
    int sector = h.h / 60, f = h.h % 60;
    int p = v * (100 - s) / 100;
    int q = v * (100 - s * f / 60) / 100;
    int t = v * (100 - s * (60 - f) / 60) / 100;
    int vv = v;
    RGB c;
    switch (sector) {
        case 0: c.r = vv; c.g = t; c.b = p; break;
        case 1: c.r = q; c.g = vv; c.b = p; break;
        case 2: c.r = p; c.g = vv; c.b = t; break;
        case 3: c.r = p; c.g = q; c.b = vv; break;
        case 4: c.r = t; c.g = p; c.b = vv; break;
        default: c.r = vv; c.g = p; c.b = q; break;
    }
    c.r = c.r * 255 / 100;
    c.g = c.g * 255 / 100;
    c.b = c.b * 255 / 100;
    return c;
}

HSV rgb_to_hsv(const RGB& c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int mn = c.r < c.g ? (c.r < c.b ? c.r : c.b) : (c.g < c.b ? c.g : c.b);
    int d = mx - mn;
    HSV h;
    h.v = mx * 100 / 255;
    if (d == 0) { h.h = 0; h.s = 0; return h; }
    h.s = d * 100 / mx;
    if (mx == c.r)      h.h = 60 * (c.g - c.b) / d + (c.g < c.b ? 360 : 0);
    else if (mx == c.g) h.h = 60 * (c.b - c.r) / d + 120;
    else                h.h = 60 * (c.r - c.g) / d + 240;
    if (h.h >= 360) h.h -= 360;
    return h;
}

// =====================================================================
// YUV / YCbCr (BT.601, integer-friendly)
// =====================================================================
YUV rgb_to_yuv(const RGB& c) {
    // full-range BT.601 (256x coefficients): Y=0.299R+0.587G+0.114B etc.
    YUV o;
    o.y = (77 * c.r + 150 * c.g + 29 * c.b + 128) >> 8;
    o.u = (-43 * c.r - 85 * c.g + 128 * c.b + 128) >> 8;
    o.v = (128 * c.r - 107 * c.g - 21 * c.b + 128) >> 8;
    return o;
}

RGB yuv_to_rgb(const YUV& y) {
    // integer BT.601: r = y + 1.402v, g = y - 0.344u - 0.714v, b = y + 1.772u
    int r = y.y + ((359 * y.v + 128) >> 8);
    int g = y.y - ((88 * y.u + 128) >> 8) - ((183 * y.v + 128) >> 8);
    int b = y.y + ((454 * y.u + 128) >> 8);
    return rgb(r, g, b);
}

YCbCr rgb_to_ycbcr(const RGB& c) {
    YCbCr o;
    o.y = (77 * c.r + 150 * c.g + 29 * c.b + 128) >> 8;
    o.cb = ((-43 * c.r - 85 * c.g + 128 * c.b + 128) >> 8) + 128;
    o.cr = ((128 * c.r - 107 * c.g - 21 * c.b + 128) >> 8) + 128;
    return o;
}

RGB ycbcr_to_rgb(const YCbCr& y) {
    int cr = y.cr - 128, cb = y.cb - 128;
    int r = y.y + ((359 * cr + 128) >> 8);
    int g = y.y - ((88 * cb + 128) >> 8) - ((183 * cr + 128) >> 8);
    int b = y.y + ((454 * cb + 128) >> 8);
    return rgb(r, g, b);
}

// =====================================================================
// CMYK
// =====================================================================
CMYK rgb_to_cmyk(const RGB& c) {
    int mx = c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b);
    int kk = 255 - mx;
    CMYK o;
    o.k = kk * 100 / 255;
    if (kk >= 255) { o.c = o.m = o.y = 0; return o; }
    int den = 255 - kk;
    o.c = (255 - c.r - kk) * 100 / den;
    o.m = (255 - c.g - kk) * 100 / den;
    o.y = (255 - c.b - kk) * 100 / den;
    return o;
}

RGB cmyk_to_rgb(const CMYK& k) {
    // R = (1-C)(1-K), channels 0..100
    int r = (100 - k.c) * (100 - k.k) / 100;
    int g = (100 - k.m) * (100 - k.k) / 100;
    int b = (100 - k.y) * (100 - k.k) / 100;
    return rgb(r * 255 / 100, g * 255 / 100, b * 255 / 100);
}

// =====================================================================
// analysis / adjustments
// =====================================================================
int rgb_luma(const RGB& c) {
    return (299 * c.r + 587 * c.g + 114 * c.b) / 1000;
}

RGB rgb_grayscale(const RGB& c) {
    int g = rgb_luma(c);
    return rgb(g, g, g);
}

// integer square root (64-bit input, 32-bit result)
static unsigned isqrt64(unsigned long long x) {
    unsigned long long r = 0, b = 0x40000000ull;
    while (b) {
        unsigned long long t = r + b;
        if (t * t <= x) r = t;
        b >>= 1;
    }
    return (unsigned)r;
}

RGB rgb_gamma(const RGB& c, int gamma_x100) {
    // integer power curve: out = 255 * (v/255)^(1/gamma).
    // gamma>=2 applies repeated sqrt; gamma<=0.5 squares; else linear.
    if (gamma_x100 <= 0) return c;
    auto apply = [&](int v) {
        if (v <= 0) return 0;
        if (v >= 255) return 255;
        long long n = ((long long)v << 16) / 255;   // Q16.16 in [0,1]
        long long g = gamma_x100;
        long long out = n;
        while (g >= 200) {                          // sqrt
            out = (long long)isqrt64((unsigned long long)(out << 16));
            g -= 100;
        }
        while (g <= 50 && g > 0) {                  // square
            out = (out * out) >> 16;
            g += 100;
        }
        (void)g;
        long long vv = (out * 255) >> 16;
        return (int)(vv < 0 ? 0 : (vv > 255 ? 255 : vv));
    };
    return rgb(apply(c.r), apply(c.g), apply(c.b));
}

RGB rgb_brightness(const RGB& c, int delta) {
    return rgb(c.r + delta, c.g + delta, c.b + delta);
}

RGB rgb_contrast(const RGB& c, int factor_x100) {
    // out = (in - 128) * f + 128
    auto apply = [&](int v) {
        int x = ((v - 128) * factor_x100) / 100 + 128;
        return x < 0 ? 0 : (x > 255 ? 255 : x);
    };
    return rgb(apply(c.r), apply(c.g), apply(c.b));
}

// =====================================================================
// palettes
// =====================================================================
RGB palette_rainbow(int t) {
    HSV h;
    h.h = (int)((long long)t * 360 / 256);
    h.s = 100;
    h.v = 100;
    return hsv_to_rgb(h);
}

RGB palette_heatmap(int t) {
    if (t < 64) {       // black -> red
        int v = t * 255 / 63;
        return rgb(v, 0, 0);
    }
    if (t < 128) {      // red -> yellow
        int v = (t - 64) * 255 / 63;
        return rgb(255, v, 0);
    }
    if (t < 192) {      // yellow -> white
        int v = (t - 128) * 255 / 63;
        return rgb(255, 255, v);
    }
    return rgb(255, 255, 255);   // plateau white
}

RGB palette_plasma(int t) {
    // purple -> magenta -> orange sweep (approximation of classic plasma)
    int r = 40 + t * 215 / 255;
    int g = (t < 128) ? t * 2 : (255 - (t - 128) * 2);
    int b = 255 - t;
    return rgb(r, g, b);
}

RGB palette_grayscale(int t) {
    return rgb(t, t, t);
}

RGB lerp_rgb(const RGB& a, const RGB& b, int t) {
    if (t <= 0) return a;
    if (t >= 255) return b;
    return rgb(a.r + (b.r - a.r) * t / 255,
               a.g + (b.g - a.g) * t / 255,
               a.b + (b.b - a.b) * t / 255);
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_color_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_color_fails++;
    (void)what;
}
bool near(const RGB& a, const RGB& b, int tol = 3) {
    return a.r >= b.r - tol && a.r <= b.r + tol &&
           a.g >= b.g - tol && a.g <= b.g + tol &&
           a.b >= b.b - tol && a.b <= b.b + tol;
}
} // namespace

int color_self_test() {
    g_color_fails = 0;

    // pack/unpack
    RGB c = rgb(10, 20, 30);
    uint32_t px = rgb_pack(c);
    expect("pack", px == 0xFF0A141Eu);
    RGB c2 = rgb_unpack(px);
    expect("unpack", c2.r == 10 && c2.g == 20 && c2.b == 30);
    // clamping
    RGB cl = rgb(-5, 300, 100);
    expect("clamp", cl.r == 0 && cl.g == 255 && cl.b == 100);

    // red <-> HSL round trip
    RGB red = rgb(255, 0, 0);
    HSL h = rgb_to_hsl(red);
    expect("red-hsl-h", h.h == 0);
    expect("red-hsl-s", h.s == 100);
    expect("red-hsl-l", h.l >= 49 && h.l <= 50);
    RGB back = hsl_to_rgb(h);
    expect("red-roundtrip", near(back, red, 4));

    // green: h=120
    HSL gh = rgb_to_hsl(rgb(0, 255, 0));
    expect("green-hue", gh.h == 120);
    RGB gback = hsl_to_rgb(gh);
    expect("green-roundtrip", near(gback, rgb(0, 255, 0), 4));

    // gray has s=0
    HSL gr = rgb_to_hsl(rgb(128, 128, 128));
    expect("gray-s", gr.s == 0);
    expect("gray-l", gr.l >= 49 && gr.l <= 51);

    // HSV: pure red v=100 s=100
    HSV hv = rgb_to_hsv(red);
    expect("red-hsv", hv.h == 0 && hv.s == 100 && hv.v == 100);
    RGB hb = hsv_to_rgb(hv);
    expect("hsv-roundtrip", near(hb, red, 4));
    // cyan: h=180
    HSV ch = rgb_to_hsv(rgb(0, 255, 255));
    expect("cyan-hue", ch.h == 180);

    // luma of mid-gray = 128ish
    expect("luma-gray", rgb_luma(rgb(128, 128, 128)) >= 125 &&
                        rgb_luma(rgb(128, 128, 128)) <= 131);
    expect("luma-black", rgb_luma(rgb(0, 0, 0)) == 0);
    expect("luma-white", rgb_luma(rgb(255, 255, 255)) == 255);

    // grayscale keeps luma-ish balance
    RGB gs = rgb_grayscale(rgb(100, 200, 50));
    expect("gray-eq", gs.r == gs.g && gs.g == gs.b);

    // brightness / contrast
    RGB br = rgb_brightness(rgb(100, 100, 100), 30);
    expect("brightness", br.r == 130);
    RGB ct = rgb_contrast(rgb(128, 128, 128), 200);
    expect("contrast-mid", ct.r == 128);
    RGB ct2 = rgb_contrast(rgb(0, 0, 0), 200);
    expect("contrast-black-clamp", ct2.r == 0 && ct2.g == 0);

    // YUV round trip
    YUV y = rgb_to_yuv(rgb(10, 200, 30));
    RGB yr = yuv_to_rgb(y);
    expect("yuv-roundtrip", near(yr, rgb(10, 200, 30), 8));

    // CMYK: pure red -> c0 m100 y100 k0
    CMYK k = rgb_to_cmyk(red);
    expect("cmyk-red", k.m == 100 && k.y == 100 && k.c == 0 && k.k == 0);
    RGB kr = cmyk_to_rgb(k);
    expect("cmyk-roundtrip", near(kr, red, 4));

    // palettes: endpoints
    RGB r0 = palette_rainbow(0);
    expect("rainbow-0", r0.r >= 250 && r0.b <= 5);        // red
    RGB r128 = palette_rainbow(128);
    expect("rainbow-128", r128.g >= 250);                  // green
    RGB h0 = palette_heatmap(0);
    expect("heat-0", h0.r == 0 && h0.g == 0 && h0.b == 0);
    RGB h255 = palette_heatmap(255);
    expect("heat-255", h255.r == 255 && h255.g == 255 && h255.b == 255);
    RGB g0 = palette_grayscale(0), g255 = palette_grayscale(255);
    expect("gray-pal", g0.r == 0 && g255.r == 255);

    // lerp midpoints
    RGB la = rgb(0, 0, 0), lb = rgb(255, 255, 255);
    RGB lm = lerp_rgb(la, lb, 128);
    expect("lerp-mid", lm.r == 128 && lm.g == 128 && lm.b == 128);
    expect("lerp-clamp", lerp_rgb(la, lb, -1).r == 0 && lerp_rgb(la, lb, 999).r == 255);

    return g_color_fails;
}

} // namespace gfxlib
} // namespace nefu
