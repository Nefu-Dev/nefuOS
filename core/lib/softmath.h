// nefuOS fixed-point math library (Q16.16)
// Integer-only math for the bare-metal kernel (no FPU, no SSE).
// fix = int32_t, value = x / 65536.  Range approx ±32767.
#pragma once
#include <stdint.h>

namespace nefu {
namespace fx {

typedef int32_t fix;
typedef int64_t  fix64;

// constants (Q16.16)
static const fix FX_PI    = 205887;   // 3.14159
static const fix FX_2PI   = 411775;   // 6.28319
static const fix FX_PI_2  = 102944;   // 1.57080
static const fix FX_E     = 178145;   // 2.71828
static const fix FX_LN2   = 45426;    // 0.69315
static const fix FX_LN10  = 150900;   // 2.30259
static const fix FX_HALF  = 32768;    // 0.5
static const fix FX_ONE   = 65536;    // 1.0
static const fix FX_TWO   = 131072;   // 2.0

// conversions
inline fix itofix(int i)        { return (fix)i << 16; }
inline int  fixtoi(fix f)       { return (int)(f >> 16); }
inline fix  fxf(int num, int den){ return (fix)(((fix64)num << 16) / den); }
inline fix  fx_abs(fix a)       { return a < 0 ? (fix)-a : a; }
inline fix  fx_floor(fix a)     { return a & ~(fix)0xFFFF; }
inline fix  fx_ceil(fix a)      { return (a + 0xFFFF) & ~(fix)0xFFFF; }
inline fix  fx_frac(fix a)      { return a & 0xFFFF; }

inline fix fx_mul(fix a, fix b) {
    return (fix)(((fix64)a * b) >> 16);
}
inline fix fx_div(fix a, fix b) {
    if (b == 0) return a < 0 ? (fix)-0x7FFFFFFF : 0x7FFFFFFF;
    return (fix)(((fix64)a << 16) / b);
}

fix fx_sqrt(fix a);
fix fx_sin(fix rad);
fix fx_cos(fix rad);
fix fx_tan(fix rad);
fix fx_asin(fix x);
fix fx_acos(fix x);
fix fx_atan(fix x);
fix fx_atan2(fix y, fix x);
fix fx_exp(fix x);
fix fx_ln(fix x);
fix fx_log2(fix x);
fix fx_log10(fix x);
fix fx_pow(fix b, fix e);
fix fx_deg2rad(fix deg);      // deg * pi / 180
fix fx_rad2deg(fix rad);      // rad * 180 / pi

} // namespace fx
} // namespace nefu
