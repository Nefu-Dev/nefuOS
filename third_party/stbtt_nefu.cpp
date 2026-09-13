// nefuOS integration for stb_truetype.h (public domain, Sean Barrett / RAD Game Tools).
// Bare-metal friendly: no libc, no floating point, memory via kalloc/kfree.
// Only the integer fixed-point packing API (stbtt_PackBegin / stbtt_PackFontRange /
// stbtt_PackEnd) is used by the GUI; it never calls sqrt/pow/fabs.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stddef.h>
#include <stdint.h>
namespace nefu { void* kalloc(size_t sz); void kfree(void* p); }
extern "C" {
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
}
#define STBTT_malloc(x,u)    nefu::kalloc((size_t)(x))
#define STBTT_free(x,u)      nefu::kfree((x))
#define STBTT_memcpy         memcpy
#define STBTT_memset         memset
#define STBTT_assert(x)      ((void)0)
#define STBTT_ifloor(x)      ((int)(x))
#define STBTT_iceil(x)       ((int)(x))
#define STBTT_sqrt(x)        stbtt_int_isqrt((int)(x))
#define STBTT_pow(x,y)       (x)
#define STBTT_fabs(x)        ((x) < 0 ? -(x) : (x))
#include "stb_truetype.h"

// integer sqrt helper used if any code path references STBTT_sqrt
int stbtt_int_isqrt(int n) {
    int r = 0;
    int bit = 1 << 30;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else { r >>= 1; }
        bit >>= 2;
    }
    return r;
}
