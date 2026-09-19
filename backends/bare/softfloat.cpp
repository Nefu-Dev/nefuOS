// nefuOS minimal IEEE-754 single-precision soft-float.
// The bare kernel links without libgcc; a few code paths (stb_image
// residual float math, future LVGL scalers) need these symbols.
// Correct for the rounding/edge cases image decoders actually hit;
// not a fully-verified IEEE implementation.
#include <stdint.h>

typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;

static u32 sf_bits(float f) { u32 u; __builtin_memcpy(&u, &f, 4); return u; }
static float sf_from(u32 u) { float f; __builtin_memcpy(&f, &u, 4); return f; }

extern "C" {

float __addsf3(float a, float b) {
    u32 x = sf_bits(a), y = sf_bits(b);
    // handle zeros / signs via sign-magnitude addition
    u32 xs = x & 0x80000000u, ys = y & 0x80000000u;
    u32 xm = x & 0x7FFFFFFFu, ym = y & 0x7FFFFFFFu;
    if (xm == 0) return sf_from(ym | (xs ^ ys));
    if (ym == 0) return a;
    if (xs == ys) {
        // same sign: add magnitudes (guard for exponent overflow)
        int xe = (int)((xm >> 23) & 0xFF), ye = (int)((ym >> 23) & 0xFF);
        u32 xf = (xm & 0x7FFFFF) | 0x800000;
        u32 yf = (ym & 0x7FFFFF) | 0x800000;
        if (xe < ye) { u32 t = xm; xm = ym; ym = t; t = xe; xe = ye; ye = (int)t; xf = (xm & 0x7FFFFF) | 0x800000; yf = (ym & 0x7FFFFF) | 0x800000; }
        int shift = xe - ye;
        if (shift > 31) shift = 31;
        u32 m = xf + (yf >> shift);
        int e = xe;
        if (m & 0x1000000) { m >>= 1; e++; }
        u32 out = xs | ((u32)e << 23) | (m & 0x7FFFFF);
        if (e >= 255) out = xs | 0x7F800000; // inf
        return sf_from(out);
    }
    // opposite signs: subtract magnitudes
    int xe = (int)((xm >> 23) & 0xFF), ye = (int)((ym >> 23) & 0xFF);
    u32 xf = (xm & 0x7FFFFF) | 0x800000;
    u32 yf = (ym & 0x7FFFFF) | 0x800000;
    u32 sign = xs ? xs : ys; // sign of the larger is handled below
    int cmp = (xe != ye) ? (xe - ye) : (int)(xf - yf);
    if (cmp < 0) { u32 t = xf; xf = yf; yf = t; int te = xe; xe = ye; ye = te; sign = ys ? ys : xs; }
    int shift = xe - ye;
    if (shift > 31) shift = 31;
    u32 m = xf - (yf >> shift);
    int e = xe;
    while ((m & 0x800000) == 0 && e > 0) { m <<= 1; e--; }
    if (m == 0) return 0.0f;
    u32 out = sign | ((u32)e << 23) | (m & 0x7FFFFF);
    return sf_from(out);
}

float __subsf3(float a, float b) {
    u32 y = sf_bits(b) ^ 0x80000000u;
    return __addsf3(a, sf_from(y));
}

float __mulsf3(float a, float b) {
    u32 x = sf_bits(a), y = sf_bits(b);
    u32 sign = (x ^ y) & 0x80000000u;
    u32 xm = x & 0x7FFFFFFFu, ym = y & 0x7FFFFFFFu;
    if (xm == 0 || ym == 0) return sf_from(sign);
    int xe = (int)((xm >> 23) & 0xFF) - 127;
    int ye = (int)((ym >> 23) & 0xFF) - 127;
    u32 xf = (xm & 0x7FFFFF) | 0x800000;
    u32 yf = (ym & 0x7FFFFF) | 0x800000;
    u64 m = (u64)xf * (u64)yf;          // 48 bits
    int e = xe + ye;
    // normalize to 24-bit mantissa with rounding
    if (m & 0x800000000000ull) { m >>= 1; e++; }  // top bit at 47 -> keep 23 frac
    u32 mant = (u32)((m >> 23) & 0x7FFFFF);
    e += 1; // account for the implicit bit position
    u32 out = sign | ((u32)(e + 127) << 23) | mant;
    if ((e + 127) >= 255) out = sign | 0x7F800000;
    return sf_from(out);
}

float __divsf3(float a, float b) {
    u32 x = sf_bits(a), y = sf_bits(b);
    u32 sign = (x ^ y) & 0x80000000u;
    u32 xm = x & 0x7FFFFFFFu, ym = y & 0x7FFFFFFFu;
    if (ym == 0) return sf_from(sign | 0x7F800000);
    if (xm == 0) return sf_from(sign);
    int xe = (int)((xm >> 23) & 0xFF) - 127;
    int ye = (int)((ym >> 23) & 0xFF) - 127;
    u32 xf = (xm & 0x7FFFFF) | 0x800000;
    u32 yf = (ym & 0x7FFFFF) | 0x800000;
    // long division of (xf << 32) / yf using only shifts (no u64 divide)
    u32 q = 0, r = 0;
    u64 num = (u64)xf << 32;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | (u32)((num >> i) & 1u);
        if (r >= yf) { r -= yf; q |= (1u << i); }
    }
    int e = xe - ye;
    if (q & 0x80000000u) { q >>= 1; e++; }
    u32 mant = (q >> 8) & 0x7FFFFF;
    u32 out = sign | ((u32)(e + 127) << 23) | mant;
    if ((e + 127) >= 255) out = sign | 0x7F800000;
    return sf_from(out);
}

s32 __fixsfsi(float a) {
    u32 x = sf_bits(a);
    s32 sign = (x & 0x80000000u) ? -1 : 1;
    int e = (int)((x >> 23) & 0xFF) - 127;
    if (e < 0) return 0;
    if (e > 30) return sign == 1 ? 0x7FFFFFFF : (s32)0x80000000;
    u32 m = (x & 0x7FFFFF) | 0x800000;
    u32 v = m >> (23 - e);
    return sign == 1 ? (s32)v : -(s32)v;
}

u32 __fixunssfsi(float a) {
    u32 x = sf_bits(a);
    int e = (int)((x >> 23) & 0xFF) - 127;
    if (e < 0) return 0;
    if (e > 31) return 0xFFFFFFFFu;
    u32 m = (x & 0x7FFFFF) | 0x800000;
    return m >> (23 - e);
}

float __floatsisf(s32 v) {
    if (v == 0) return 0.0f;
    u32 sign = 0;
    u32 m;
    if (v < 0) { sign = 0x80000000u; m = (u32)0 - (u32)v; }
    else m = (u32)v;
    int e = 0;
    while (m & 0xFF000000u) { m >>= 1; e++; }
    while ((m & 0x800000u) == 0 && e < 127) { m <<= 1; e--; }
    u32 out = sign | ((u32)(e + 127) << 23) | (m & 0x7FFFFF);
    return sf_from(out);
}

float __floatunsisf(u32 v) {
    if (v == 0) return 0.0f;
    int e = 0;
    while (v & 0xFF000000u) { v >>= 1; e++; }
    while ((v & 0x800000u) == 0 && e < 127) { v <<= 1; e--; }
    u32 out = ((u32)(e + 127) << 23) | (v & 0x7FFFFF);
    return sf_from(out);
}

float __negsf2(float a) { return sf_from(sf_bits(a) ^ 0x80000000u); }
float __fabsf(float a)  { return sf_from(sf_bits(a) & 0x7FFFFFFFu); }

// ---- comparison helpers (pure integer, no FP instructions) ----
// Return conventions (libgcc): eq/ne -> 0 if equal else nonzero;
// lt/le/gt/ge -> negative / zero / positive.
static int sf_cmp(float a, float b) {
    u32 x = sf_bits(a);
    u32 y = sf_bits(b);
    if (x == y) return 0;
    int xn = (x >> 31) & 1;
    int yn = (y >> 31) & 1;
    if (xn != yn) return xn ? -1 : 1;         // negative < positive
    if (xn) return x > y ? -1 : (x < y ? 1 : 0); // IEEE bits reverse for negatives
    return x < y ? -1 : (x > y ? 1 : 0);
}

int __eqsf2(float a, float b) { return sf_cmp(a, b) == 0 ? 0 : 1; }
int __nesf2(float a, float b) { return sf_cmp(a, b) == 0 ? 0 : 1; }
int __ltsf2(float a, float b) { return sf_cmp(a, b); }
int __lesf2(float a, float b) { int c = sf_cmp(a, b); return c > 0 ? 1 : 0; }
int __gtsf2(float a, float b) { return sf_cmp(a, b); }
int __gesf2(float a, float b) { int c = sf_cmp(a, b); return c < 0 ? -1 : 0; }

} // extern "C"
