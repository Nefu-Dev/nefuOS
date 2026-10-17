// nefuOS stdcxx — <limits>
#pragma once
#include "_core.h"
namespace nefu {
namespace stdx {

template <class T>
class numeric_limits {
public:
    static const bool is_specialized = false;
    static const int digits = 0;
    static const bool is_signed = false;
    static const bool is_integer = false;
    static T min() { return T(); }
    static T max() { return T(); }
    static T lowest() { return T(); }
};

#define NEFU_STDX_NUMERIC_LIMITS(T, MN, MX, SIGN, INTGR, DG) \
template <> class numeric_limits<T> { \
public: \
    static const bool is_specialized = true; \
    static const int  digits = DG; \
    static const bool is_signed  = SIGN; \
    static const bool is_integer = INTGR; \
    static T min()   { return (T)(MN); } \
    static T max()   { return (T)(MX); } \
    static T lowest(){ return min(); } \
};

NEFU_STDX_NUMERIC_LIMITS(bool, false, true, false, true, 1)
NEFU_STDX_NUMERIC_LIMITS(char, (-128), 127, true, true, 8)
NEFU_STDX_NUMERIC_LIMITS(signed char, (-128), 127, true, true, 8)
NEFU_STDX_NUMERIC_LIMITS(unsigned char, 0, 255, false, true, 8)
NEFU_STDX_NUMERIC_LIMITS(short, (-32768), 32767, true, true, 16)
NEFU_STDX_NUMERIC_LIMITS(unsigned short, 0, 65535, false, true, 16)
NEFU_STDX_NUMERIC_LIMITS(int, (-2147483647 - 1), 2147483647, true, true, 32)
NEFU_STDX_NUMERIC_LIMITS(unsigned int, 0, 4294967295u, false, true, 32)
NEFU_STDX_NUMERIC_LIMITS(long, (-2147483647L - 1L), 2147483647L, true, true, 32)
NEFU_STDX_NUMERIC_LIMITS(unsigned long, 0UL, 4294967295UL, false, true, 32)
NEFU_STDX_NUMERIC_LIMITS(long long, (-9223372036854775807LL - 1LL), 9223372036854775807LL, true, true, 64)
NEFU_STDX_NUMERIC_LIMITS(unsigned long long, 0ULL, 18446744073709551615ULL, false, true, 64)

#undef NEFU_STDX_NUMERIC_LIMITS

template <>
class numeric_limits<float> {
public:
    static const bool is_specialized = true;
    static const int  digits = 24;
    static const bool is_signed = true;
    static const bool is_integer = false;
    static float min()    { return 1.17549435e-38f; }
    static float max()    { return 3.40282347e+38f; }
    static float lowest() { return -3.40282347e+38f; }
};
template <>
class numeric_limits<double> {
public:
    static const bool is_specialized = true;
    static const int  digits = 53;
    static const bool is_signed = true;
    static const bool is_integer = false;
    static double min()    { return 2.2250738585072014e-308; }
    static double max()    { return 1.7976931348623157e+308; }
    static double lowest() { return -1.7976931348623157e+308; }
};

} // namespace stdx
} // namespace nefu
