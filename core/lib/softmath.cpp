// nefuOS fixed-point math library — implementation (Q16.16, integer only)
#include "softmath.h"

namespace nefu {
namespace fx {

fix fx_sqrt(fix a) {
    if (a <= 0) return 0;
    // Newton's method on the scaled value.
    // sqrt(a) in Q16.16: iterate x = (x + a/x)/2
    fix x = a > 0x40000000 ? (fix)0x40000000 : a;
    // better initial guess: sqrt(a) ~= a >> 1 clamped
    x = a >> 1;
    if (x < 1) x = 1;
    for (int i = 0; i < 12; i++) {
        fix nx = fx_div(a, x);
        x = (x + nx) >> 1;
        if (fx_mul(x, x) == a || fx_mul(x, x) == a + 1) break;
    }
    return x;
}

// ---- sin / cos via Taylor with range reduction ----
// reduce rad into [0, 2*pi)
static fix reduce_rad(fix rad) {
    fix r = rad % FX_2PI;
    if (r < 0) r += FX_2PI;
    return r;
}

fix fx_sin(fix rad) {
    fix r = reduce_rad(rad);
    bool neg = false;
    if (r > FX_PI) { r -= FX_PI; neg = true; }
    if (r > FX_PI_2) r = FX_PI - r;          // [0, pi/2]
    // sin(x) = x - x^3/6 + x^5/120 - x^7/5040 + x^9/362880 - x^11/39916800
    // (Q16.16 value / integer n == fix / n, so plain integer division is
    //  exact for these constant denominators; itofix(362880) would overflow)
    fix x = r;
    fix x2 = fx_mul(x, x);
    fix x3 = fx_mul(x, x2);
    fix x5 = fx_mul(x3, x2);
    fix x7 = fx_mul(x5, x2);
    fix x9 = fx_mul(x7, x2);
    fix x11 = fx_mul(x9, x2);
    fix s = x;
    s -= x3 / 6;
    s += x5 / 120;
    s -= x7 / 5040;
    s += x9 / 362880;
    s -= x11 / 39916800;
    return neg ? (fix)-s : s;
}

fix fx_cos(fix rad) {
    fix r = reduce_rad(rad);
    bool neg = false;
    if (r > FX_PI) { r -= FX_PI; neg = true; }
    if (r > FX_PI_2) { r = FX_PI - r; neg = !neg; }
    // cos(x) = 1 - x^2/2 + x^4/24 - x^6/720 + x^8/40320 - x^10/3628800
    // (integer denominators: Q16.16 value / n == fix / n)
    fix x = r;
    fix x2 = fx_mul(x, x);
    fix x4 = fx_mul(x2, x2);
    fix x6 = fx_mul(x4, x2);
    fix x8 = fx_mul(x6, x2);
    fix x10 = fx_mul(x8, x2);
    fix c = FX_ONE;
    c -= x2 / 2;
    c += x4 / 24;
    c -= x6 / 720;
    c += x8 / 40320;
    c -= x10 / 3628800;
    return neg ? (fix)-c : c;
}

fix fx_tan(fix rad) {
    fix c = fx_cos(rad);
    if (c == 0) return rad > 0 ? 0x7FFFFFFF : (fix)-0x7FFFFFFF;
    return fx_div(fx_sin(rad), c);
}

// atan(x) for |x| <= 1, Taylor:
// atan(x) = x - x^3/3 + x^5/5 - x^7/7 + x^9/9 - x^11/11 + x^13/13
static fix atan_series(fix x) {
    fix x2 = fx_mul(x, x);
    fix x3 = fx_mul(x, x2);
    fix x5 = fx_mul(x3, x2);
    fix x7 = fx_mul(x5, x2);
    fix x9 = fx_mul(x7, x2);
    fix x11 = fx_mul(x9, x2);
    fix x13 = fx_mul(x11, x2);
    fix s = x;
    s -= fx_div(x3, itofix(3));
    s += fx_div(x5, itofix(5));
    s -= fx_div(x7, itofix(7));
    s += fx_div(x9, itofix(9));
    s -= fx_div(x11, itofix(11));
    s += fx_div(x13, itofix(13));
    return s;
}

fix fx_atan(fix x) {
    bool neg = x < 0;
    if (neg) x = (fix)-x;
    fix r;
    if (x > FX_ONE) {
        // atan(x) = pi/2 - atan(1/x)
        fix inv = fx_div(FX_ONE, x);
        r = FX_PI_2 - atan_series(inv);
    } else if (x > FX_HALF) {
        // atan(x) = atan(0.5) + atan((x-0.5)/(1+0.5x))
        fix a = fx_div(x - FX_HALF, FX_ONE + fx_mul(FX_HALF, x));
        r = 30382 + atan_series(a); // atan(0.5) ~= 0.463648 rad = 30382 Q16.16
    } else {
        r = atan_series(x);
    }
    return neg ? (fix)-r : r;
}

fix fx_atan2(fix y, fix x) {
    if (x == 0) {
        if (y > 0) return FX_PI_2;
        if (y < 0) return (fix)-FX_PI_2;
        return 0;
    }
    fix a = fx_atan(fx_div(y, x));
    if (x < 0) {
        if (y >= 0) return a + FX_PI;
        return a - FX_PI;
    }
    return a;
}

fix fx_asin(fix x) {
    if (x >= FX_ONE) return FX_PI_2;
    if (x <= (fix)-FX_ONE) return (fix)-FX_PI_2;
    return fx_atan(fx_div(x, fx_sqrt(FX_ONE - fx_mul(x, x))));
}

fix fx_acos(fix x) {
    return FX_PI_2 - fx_asin(x);
}

// ---- exp / ln ----
fix fx_exp(fix x) {
    // x = k*ln2 + r, r in [-0.5, 0.5]
    int k = fixtoi(fx_div(x, FX_LN2));
    fix r = x - fx_mul(itofix(k), FX_LN2);
    if (r > FX_HALF) { k++; r -= FX_LN2; }
    else if (r < (fix)-FX_HALF) { k--; r += FX_LN2; }
    // exp(r) = 1 + r + r^2/2 + r^3/6 + r^4/24 + r^5/120 + r^6/720 + r^7/5040
    fix r2 = fx_mul(r, r);
    fix r3 = fx_mul(r2, r);
    fix r4 = fx_mul(r3, r);
    fix r5 = fx_mul(r4, r);
    fix r6 = fx_mul(r5, r);
    fix e = FX_ONE;
    e += r;
    e += fx_div(r2, itofix(2));
    e += fx_div(r3, itofix(6));
    e += fx_div(r4, itofix(24));
    e += fx_div(r5, itofix(120));
    e += fx_div(r6, itofix(720));
    // multiply by 2^k
    if (k > 0) {
        if (k > 15) k = 15;
        for (int i = 0; i < k; i++) e <<= 1;
    } else if (k < 0) {
        if (k < -15) k = -15;
        for (int i = 0; i < -k; i++) e >>= 1;
    }
    return e;
}

fix fx_ln(fix x) {
    if (x <= 0) return 0;
    // find k such that m = x >> k in [0.75, 1.5)
    int k = 0;
    fix m = x;
    while (m >= (fix)(3 * FX_ONE / 2)) { m >>= 1; k++; }
    while (m < (fix)(3 * FX_ONE / 4)) { m <<= 1; k--; }
    // ln(m) = 2*(z + z^3/3 + z^5/5 + z^7/7 + z^9/9)  z = (m-1)/(m+1)
    fix z = fx_div(m - FX_ONE, m + FX_ONE);
    fix z2 = fx_mul(z, z);
    fix z3 = fx_mul(z, z2);
    fix z5 = fx_mul(z3, z2);
    fix z7 = fx_mul(z5, z2);
    fix z9 = fx_mul(z7, z2);
    fix s = z;
    s += fx_div(z3, itofix(3));
    s += fx_div(z5, itofix(5));
    s += fx_div(z7, itofix(7));
    s += fx_div(z9, itofix(9));
    // ln(m) = 2*s
    return (s << 1) + fx_mul(itofix(k), FX_LN2);
}

fix fx_log2(fix x) { return fx_div(fx_ln(x), FX_LN2); }
fix fx_log10(fix x) { return fx_div(fx_ln(x), FX_LN10); }

fix fx_pow(fix b, fix e) {
    if (b <= 0) return 0;
    return fx_exp(fx_mul(e, fx_ln(b)));
}

fix fx_deg2rad(fix deg) { return fx_div(fx_mul(deg, FX_PI), itofix(180)); }
fix fx_rad2deg(fix rad) { return fx_div(fx_mul(rad, itofix(180)), FX_PI); }

} // namespace fx
} // namespace nefu
