// nefuOS soft-float math library (single precision).
// All arithmetic uses the C++ `float` type, which the bare-metal kernel
// provides through the GCC soft-float library (no hardware FPU needed).
// The transcendental functions are implemented here with range reduction +
// polynomial/Taylor evaluation so we never depend on libm.
// Also provides decimal string parsing (incl. scientific notation) and
// formatting (plain or scientific) for calculators.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace sf {

// ===================== constants =====================
inline constexpr float SF_PI   = 3.14159265358979323846f;
inline constexpr float SF_2PI  = 6.28318530717958647692f;
inline constexpr float SF_HALF = 1.57079632679489661923f;
inline constexpr float SF_E    = 2.71828182845904523536f;
inline constexpr float SF_LN2  = 0.69314718055994530942f;
inline constexpr float SF_LN10 = 2.30258509299404568402f;

// ===================== basic =====================
inline float sf_abs(float x)    { return x < 0.0f ? -x : x; }
inline float sf_floor(float x) {
    if (x > 1.0e9f) return 1.0e9f;
    if (x < -1.0e9f) return -1.0e9f;
    float r = (float)(int)x;
    if (x < r) r -= 1.0f;
    return r;
}
inline float sf_ceil(float x)  { return -sf_floor(-x); }
inline float sf_round(float x) { return sf_floor(x + 0.5f); }
inline float sf_fract(float x) { return x - sf_floor(x); }
inline float sf_mod(float a, float b) {
    return b == 0.0f ? a : a - b * sf_floor(a / b);
}
inline float sf_min(float a, float b) { return a < b ? a : b; }
inline float sf_max(float a, float b) { return a > b ? a : b; }
inline float sf_clamp(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}
inline float sf_sign(float x) {
    return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
}

// ===================== sqrt =====================
inline float sf_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x * 0.5f + 0.5f;
    for (int i = 0; i < 14; i++) g = 0.5f * (g + x / g);
    return g;
}

// ===================== trig =====================
inline float sf_sin(float x) {
    float r = sf_mod(x, SF_2PI);
    if (r > SF_PI) r -= SF_2PI;
    if (r < -SF_PI) r += SF_2PI;
    float r2 = r * r;
    return r * (1.0f + r2 * (-0.16666666667f + r2 * (0.00833333333f +
                r2 * (-0.000198412698f + r2 * (0.00000275573192f +
                r2 * (-0.0000000250521f))))));
}
inline float sf_cos(float x) { return sf_sin(x + SF_HALF); }
inline float sf_tan(float x) {
    float c = sf_cos(x);
    return c == 0.0f ? 0.0f : sf_sin(x) / c;
}
inline float sf_atan(float z) {
    bool inv = sf_abs(z) > 1.0f;
    float a = inv ? 1.0f / z : z;
    float a2 = a * a;
    float r = a * (0.9998660f + a2 * (-0.3302995f + a2 * (0.1801410f +
              a2 * (-0.0851330f + a2 * 0.0208351f))));
    return inv ? SF_HALF - r : r;
}
inline float sf_atan2(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return SF_HALF;
        if (y < 0.0f) return -SF_HALF;
        return 0.0f;
    }
    float r = sf_atan(y / x);
    if (x < 0.0f) r += (y >= 0.0f ? SF_PI : -SF_PI);
    return r;
}
inline float sf_asin(float x) {
    x = sf_clamp(x, -1.0f, 1.0f);
    return sf_atan2(x, sf_sqrt(sf_max(0.0f, 1.0f - x * x)));
}
inline float sf_acos(float x) { return SF_HALF - sf_asin(x); }

// ===================== exp / log / pow =====================
inline float sf_exp(float x) {
    float k = sf_floor(x / SF_LN2);
    float r = x - k * SF_LN2;
    float p2 = 1.0f;
    int ik = (int)sf_clamp(k, -100.0f, 100.0f);
    if (ik >= -126 && ik <= 127) {
        uint32_t bits = (uint32_t)(ik + 127) << 23;
        __builtin_memcpy(&p2, &bits, 4);
    }
    // Taylor of e^r around 0 (r in [-ln2/2, ln2/2] -> 5 terms is ~1e-7)
    float pr = 1.0f + r * (1.0f + r * (0.5f + r * (0.16666667f +
               r * (0.041666667f + r * 0.0083333333f))));
    return p2 * pr;
}
inline float sf_exp2(float x) { return sf_exp(x * SF_LN2); }
// ln(x) via ln((1+t)/(1-t)) = 2*(t + t^3/3 + t^5/5 + t^7/7 + t^9/9)
// with t = (m-1)/(m+1) for m in [1,2): |t| <= 1/3, series converges fast.
inline float sf_ln_mant(float m) {   // m in [1,2)
    float t = (m - 1.0f) / (m + 1.0f);
    float t2 = t * t;
    float t3 = t2 * t;
    float t5 = t3 * t2;
    float t7 = t5 * t2;
    float t9 = t7 * t2;
    return 2.0f * (t + t3 / 3.0f + t5 / 5.0f + t7 / 7.0f + t9 / 9.0f);
}
inline float sf_log2(float x) {
    if (x <= 0.0f) return -1.0e30f;
    uint32_t bits;
    __builtin_memcpy(&bits, &x, 4);
    int e = (int)((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFFu) | (127u << 23);
    float m;
    __builtin_memcpy(&m, &bits, 4);
    return (float)e + sf_ln_mant(m) / SF_LN2;
}
inline float sf_log(float x) {
    if (x <= 0.0f) return -1.0e30f;
    uint32_t bits;
    __builtin_memcpy(&bits, &x, 4);
    int e = (int)((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFFu) | (127u << 23);
    float m;
    __builtin_memcpy(&m, &bits, 4);
    return (float)e * SF_LN2 + sf_ln_mant(m);
}
inline float sf_log10(float x) {
    if (x <= 0.0f) return -1.0e30f;
    uint32_t bits;
    __builtin_memcpy(&bits, &x, 4);
    int e = (int)((bits >> 23) & 0xFF) - 127;
    bits = (bits & 0x007FFFFFu) | (127u << 23);
    float m;
    __builtin_memcpy(&m, &bits, 4);
    return ((float)e * SF_LN2 + sf_ln_mant(m)) / SF_LN10;
}
inline float sf_pow(float a, float b) {
    if (b == 0.0f) return 1.0f;
    // integer exponent fast path (works for negative bases too)
    int ib = (int)b;
    if ((float)ib == b && ib >= -64 && ib <= 64) {
        float r = 1.0f;
        float base = a;
        int n = ib < 0 ? -ib : ib;
        for (int i = 0; i < n; i++) r *= base;
        return ib < 0 ? 1.0f / r : r;
    }
    if (a <= 0.0f) return 0.0f;
    return sf_exp(b * sf_log(a));
}

// ===================== string -> float =====================
// Parses decimal and scientific notation: "12.5", "-3", ".5", "1e-9", "2.5E6"
inline float sf_atof(const char* s) {
    if (!s) return 0.0f;
    int i = 0;
    bool neg = false;
    if (s[i] == '-') { neg = true; i++; }
    else if (s[i] == '+') i++;
    float ip = 0.0f;
    while (s[i] >= '0' && s[i] <= '9') { ip = ip * 10.0f + (float)(s[i] - '0'); i++; }
    float fp = 0.0f;
    float div = 1.0f;
    if (s[i] == '.') {
        i++;
        while (s[i] >= '0' && s[i] <= '9') {
            fp = fp * 10.0f + (float)(s[i] - '0');
            div *= 10.0f;
            i++;
        }
    }
    float v = ip + fp / div;
    if (s[i] == 'e' || s[i] == 'E') {
        i++;
        bool eneg = false;
        if (s[i] == '-') { eneg = true; i++; }
        else if (s[i] == '+') i++;
        int e = 0;
        while (s[i] >= '0' && s[i] <= '9') { e = e * 10 + (s[i] - '0'); if (e > 99) e = 99; i++; }
        float p10 = 1.0f;
        for (int k = 0; k < e; k++) p10 *= 10.0f;
        v *= eneg ? 1.0f / p10 : p10;
    }
    return neg ? -v : v;
}

// ===================== float -> string =====================
// digits = significant digits (1..9). Large/small magnitudes switch to
// scientific notation automatically: 1.2345e8 or 1.5e-7.
// Internally uses double so decimal scaling keeps full precision.
inline void strcpy_safe(char* dst, size_t n, const char* src) {
    if (!dst || n == 0) return;
    size_t i = 0;
    while (src[i] && i < n - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

inline void sf_to_str(char* out, size_t n, float v, int digits) {
    if (!out || n == 0) return;
    if (n == 1) { out[0] = 0; return; }
    if (digits < 1) digits = 1;
    if (digits > 9) digits = 9;
    if (v != v || v > 3.0e38f || v < -3.0e38f) {   // NaN / overflow guard
        strcpy_safe(out, n, "err");
        return;
    }
    if (v == 0.0f) { strcpy_safe(out, n, "0"); return; }
    bool neg = v < 0.0f;
    double a = neg ? -(double)v : (double)v;
    int e10 = 0;
    while (a >= 10.0) { a /= 10.0; e10++; if (e10 > 40) break; }
    while (a < 1.0)   { a *= 10.0; e10--; if (e10 < -40) break; }
    // mantissa rounded to `digits` significant digits
    long long scale = 1;
    for (int i = 0; i < digits - 1; i++) scale *= 10;
    long long m = (long long)(a * (double)scale + 0.5);
    if (m >= scale * 10) { m /= 10; e10++; }
    long long ip = m / scale;
    long long fp = m % scale;
    bool sci = (e10 >= 7 || e10 <= -6);
    char md[16];
    int mdn = 0;
    md[mdn++] = (char)('0' + (int)ip);
    long long d = scale / 10;
    for (int i = 0; i < digits - 1; i++) { md[mdn++] = (char)('0' + (int)((fp / d) % 10)); d /= 10; }
    while (mdn > 1 && md[mdn - 1] == '0') mdn--;
    size_t w = 0;
    if (neg && w < n - 1) out[w++] = '-';
    if (sci) {
        if (w < n - 1) out[w++] = md[0];
        if (mdn > 1 && w < n - 1) out[w++] = '.';
        for (int i = 1; i < mdn && w < n - 1; i++) out[w++] = md[i];
        if (w < n - 1) out[w++] = 'e';
        if (e10 < 0 && w < n - 1) out[w++] = '-';
        int ae = e10 < 0 ? -e10 : e10;
        char eb[6]; int en = 0;
        if (ae == 0) eb[en++] = '0';
        while (ae > 0 && en < 5) { eb[en++] = (char)('0' + ae % 10); ae /= 10; }
        while (en > 0 && w < n - 1) out[w++] = eb[--en];
    } else if (e10 >= 0) {
        if (e10 + 1 >= mdn) {
            for (int i = 0; i < mdn && w < n - 1; i++) out[w++] = md[i];
            int z = e10 + 1 - mdn;
            for (int i = 0; i < z && w < n - 1; i++) out[w++] = '0';
        } else {
            for (int i = 0; i <= e10 && w < n - 1; i++) out[w++] = md[i];
            if (w < n - 1) out[w++] = '.';
            for (int i = e10 + 1; i < mdn && w < n - 1; i++) out[w++] = md[i];
        }
    } else {
        if (w < n - 1) out[w++] = '0';
        if (w < n - 1) out[w++] = '.';
        int z = -e10 - 1;
        for (int i = 0; i < z && w < n - 1; i++) out[w++] = '0';
        for (int i = 0; i < mdn && w < n - 1; i++) out[w++] = md[i];
    }
    out[w] = 0;
}

} // namespace sf
} // namespace nefu
