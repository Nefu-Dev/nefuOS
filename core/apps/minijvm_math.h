// nefuOS mini JVM - soft double math (header-only, no libm).
//
// The bare-metal kernel links GCC soft-float (no hardware FPU) and has no
// libm, so every transcendental used by java/lang/Math is implemented here
// with plain + - * / and integer bit manipulation.  The host build uses the
// same code so behaviour is identical everywhere.
#pragma once

#include <stdint.h>
#include <string.h>

namespace nefu {

static inline uint32_t mj_f_to_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float mj_f_from_bits(uint64_t b) { uint32_t u = (uint32_t)b; float f; memcpy(&f, &u, 4); return f; }
static inline uint64_t mj_d_to_bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static inline double mj_d_from_bits(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }

static inline const double MJ_PI   = 3.14159265358979323846;
static inline const double MJ_2PI  = 6.28318530717958647692;
static inline const double MJ_HALF = 1.57079632679489661923;
static inline const double MJ_LN2  = 0.69314718055994530942;
static inline const double MJ_LN10 = 2.30258509299404568402;
static inline const double MJ_E    = 2.71828182845904523536;

static inline bool mj_is_nan(double x) {
    uint64_t b = mj_d_to_bits(x);
    return ((b >> 52) & 0x7FF) == 0x7FF && (b & 0xFFFFFFFFFFFFFULL) != 0;
}
static inline bool mj_is_inf(double x) {
    uint64_t b = mj_d_to_bits(x);
    return ((b >> 52) & 0x7FF) == 0x7FF && (b & 0xFFFFFFFFFFFFFULL) == 0;
}
static inline bool mj_is_finite(double x) {
    uint64_t b = mj_d_to_bits(x);
    return ((b >> 52) & 0x7FF) != 0x7FF;
}
static inline double mj_nan() { return mj_d_from_bits(0x7FF8000000000000ULL); }
static inline double mj_inf() { return mj_d_from_bits(0x7FF0000000000000ULL); }

static inline double mj_abs(double x) { return x < 0.0 ? -x : x; }
static inline double mj_min(double a, double b) { return a < b ? a : b; }
static inline double mj_max(double a, double b) { return a > b ? a : b; }

static inline double mj_trunc(double x) {
    if (x == 0.0 || !mj_is_finite(x)) return x;
    if (mj_abs(x) >= 9.2e18) return x;
    return (double)(long long)x;
}
static inline double mj_floor(double x) {
    if (x == 0.0 || !mj_is_finite(x)) return x;
    if (mj_abs(x) >= 9.2e18) return x;
    double r = (double)(long long)x;
    if (r > x) r -= 1.0;
    return r;
}
static inline double mj_ceil(double x) { return -mj_floor(-x); }
static inline double mj_round(double x) { return mj_floor(x + 0.5); }

static inline double mj_sqrt(double x) {
    if (x < 0.0) return mj_nan();
    if (x == 0.0) return 0.0;
    double g = x * 0.5 + 0.5;
    for (int i = 0; i < 14; i++) g = 0.5 * (g + x / g);
    return g;
}
static inline double mj_rem(double a, double b) {          // Java % (truncated)
    if (b == 0.0 || mj_is_nan(a) || mj_is_nan(b)) return mj_nan();
    return a - b * mj_trunc(a / b);
}

static inline double mj_sin_pi2(double x) {                // |x| <= pi/2, Taylor
    double x2 = x * x;
    return x - x * x2 / 6.0 + x * x2 * x2 / 120.0 - x * x2 * x2 * x2 / 5040.0 +
           x * x2 * x2 * x2 * x2 / 362880.0 - x * x2 * x2 * x2 * x2 * x2 / 39916800.0 +
           x * x2 * x2 * x2 * x2 * x2 * x2 / 6227020800.0;
}
static inline double mj_sin(double x) {
    double r = x - mj_floor(x / MJ_2PI) * MJ_2PI;
    if (r > MJ_PI) r -= MJ_2PI;
    if (r < -MJ_PI) r += MJ_2PI;
    if (r > MJ_HALF) return mj_sin_pi2(MJ_PI - r);
    if (r < -MJ_HALF) return -mj_sin_pi2(MJ_PI + r);
    return mj_sin_pi2(r);
}
static inline double mj_cos(double x) { return mj_sin(x + MJ_HALF); }
static inline double mj_tan(double x) {
    double c = mj_cos(x);
    if (c == 0.0) return mj_nan();
    return mj_sin(x) / c;
}
static inline double mj_atan(double z) {
    bool inv = mj_abs(z) > 1.0;
    double a = inv ? 1.0 / z : z;
    double a2 = a * a;
    double r = a * (0.9998660 + a2 * (-0.3302995 + a2 * (0.1801410 +
                 a2 * (-0.0851330 + a2 * 0.0208351))));
    if (inv) return (z >= 0 ? MJ_HALF - r : -MJ_HALF - r);
    return r;
}
static inline double mj_atan2(double y, double x) {
    if (x > 0.0) return mj_atan(y / x);
    if (x < 0.0) return (y >= 0 ? MJ_PI : -MJ_PI) + mj_atan(y / x);
    if (y > 0.0) return MJ_HALF;
    if (y < 0.0) return -MJ_HALF;
    return 0.0;
}
static inline double mj_asin(double x) {
    if (x < -1.0) x = -1.0;
    if (x > 1.0) x = 1.0;
    return mj_atan2(x, mj_sqrt(mj_max(0.0, 1.0 - x * x)));
}
static inline double mj_acos(double x) { return MJ_HALF - mj_asin(x); }

static inline double mj_exp(double x) {
    double k = mj_floor(x / MJ_LN2 + 1e-9);   // guard the integer boundary against division rounding
    if (k > 1023.0) k = 1023.0;
    if (k < -1023.0) k = -1023.0;
    double r = x - k * MJ_LN2;
    double p2 = 1.0;
    long long ik = (long long)k;
    if (ik >= 0) { for (long long i = 0; i < ik && i < 1024; i++) p2 *= 2.0; }
    else         { for (long long i = 0; i < -ik && i < 1024; i++) p2 *= 0.5; }
    double pr = 1.0 + r * (1.0 + r * (0.5 + r * (0.16666666666666666 +
                r * (0.041666666666666664 + r * (0.008333333333333333 +
                r * (0.001388888888888889 + r * 0.00019841269841269841))))));
    return p2 * pr;
}
static inline double mj_ln_mant(double m) {                // m in [1,2)
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t;
    return 2.0 * t * (1.0 + t2 * (1.0 / 3.0 + t2 * (1.0 / 5.0 + t2 * (1.0 / 7.0 +
           t2 * (1.0 / 9.0 + t2 * (1.0 / 11.0 + t2 / 13.0))))));
}
static inline double mj_ln(double x) {
    if (x < 0.0 || x == 0.0) return mj_nan();
    if (mj_is_nan(x) || mj_is_inf(x)) return x;
    uint64_t b = mj_d_to_bits(x);
    int e = (int)((b >> 52) & 0x7FF) - 1023;
    uint64_t mb = (b & 0xFFFFFFFFFFFFFULL) | 0x3FF0000000000000ULL;
    return (double)e * MJ_LN2 + mj_ln_mant(mj_d_from_bits(mb));
}
static inline double mj_log10(double x) { return mj_ln(x) / MJ_LN10; }
static inline double mj_pow(double b, double e) {
    if (b == 1.0 || e == 0.0) return 1.0;
    if (b == 0.0) return e > 0.0 ? 0.0 : mj_inf();
    // integer-exponent fast path: exact and immune to exp() rounding
    double t = mj_trunc(e);
    if (t == e && mj_abs(e) <= 1e6) {
        long long n = (long long)e;
        bool neg_pow = n < 0;
        if (neg_pow) n = -n;
        double r = 1.0;
        double base = b < 0.0 ? -b : b;
        while (n > 0) {
            if (n & 1) r *= base;
            base *= base;
            n >>= 1;
        }
        if (b < 0.0 && ((long long)e & 1)) r = -r;
        if (neg_pow) r = 1.0 / r;
        return r;
    }
    if (b < 0.0) {
        double tr = mj_trunc(e);
        if (tr == e) {
            bool odd = ((long long)e & 1) != 0;
            double p = mj_exp(e * mj_ln(-b));
            return odd ? -p : p;
        }
        return mj_nan();
    }
    return mj_exp(e * mj_ln(b));
}
static inline double mj_cbrt(double x) {
    if (x == 0.0) return 0.0;
    if (x < 0.0) return -mj_pow(-x, 1.0 / 3.0);
    return mj_pow(x, 1.0 / 3.0);
}
static inline double mj_deg2rad(double d) { return d * MJ_PI / 180.0; }
static inline double mj_rad2deg(double r) { return r * 180.0 / MJ_PI; }

} // namespace nefu
