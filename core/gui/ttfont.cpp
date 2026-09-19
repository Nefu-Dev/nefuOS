// nefuOS TrueType font renderer - stb_truetype backend.
// stb_truetype is MIT licensed (nothings/stb). Embedded font:
// Montserrat-Medium.ttf, SIL Open Font License 1.1 (Google Fonts).
// NOTE: the previous VT323-Regular.ttf embedding was truncated (table offsets
// past EOF) which made stb_truetype read out of bounds; replaced with the
// complete Montserrat-Medium.ttf (same OFL-1.1 license family).
//
// The kernel has no libc malloc, so STBTT_malloc/free are mapped to the
// nefuOS heap (kalloc/kfree). All arithmetic stays in single-precision
// floats, which the bare-metal soft-float library provides.
#include "gfx.h"
#include "ttfont.h"
#include "../klib/klib.h"
#include "mont_data.h"   // resolved via -I third_party

// ---- platform math hooks for stb_truetype ----
// The bare kernel has no libc <math.h> (g++ redirects it to <cmath>, which
// #errors under -ffreestanding), so provide minimal single-precision
// implementations built on the soft-float add/sub/mul/div primitives.
#ifdef NEFU_BARE
static inline float nefu_sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float guess = x * 0.5f + 0.5f;
    for (int i = 0; i < 6; i++) guess = 0.5f * (guess + x / guess);
    return guess;
}
static inline float nefu_fmodf(float x, float y) {
    if (y == 0.0f) return 0.0f;
    int q = (int)(x / y);
    return x - (float)q * y;
}
static inline float nefu_powf(float x, float y) {
    if (y == 0.0f) return 1.0f;
    if (x == 0.0f) return 0.0f;
    int n = (int)y;
    float r = 1.0f;
    for (int i = 0; i < n; i++) r *= x;
    float f = y - (float)n;
    if (f != 0.0f) {
        // (1+t)^f via truncated binomial series, t = x-1
        float t = x - 1.0f;
        float term = 1.0f;
        float acc = 1.0f;
        for (int i = 1; i < 5; i++) {
            term *= (f - (float)(i - 1)) / (float)i * t;
            acc += term;
        }
        r *= acc;
    }
    return r;
}
static inline float nefu_cosf(float x) {
    const float pi = 3.14159265f;
    while (x >  pi) x -= 2.0f * pi;
    while (x < -pi) x += 2.0f * pi;
    float x2 = x * x;
    // Taylor around 0, good to ~1e-4 on [-pi,pi]
    return 1.0f - x2 * 0.5f
               + x2 * x2 * (1.0f / 24.0f)
               - x2 * x2 * x2 * (1.0f / 720.0f)
               + x2 * x2 * x2 * x2 * (1.0f / 40320.0f);
}
static inline float nefu_acosf(float x) {
    if (x >=  1.0f) return 0.0f;
    if (x <= -1.0f) return 3.14159265f;
    // acos(x) = pi/2 - asin(x); asin via odd-power series
    float x2 = x * x;
    float term = x;
    float sum = x;
    for (int i = 1; i < 8; i++) {
        term *= x2 * (float)(2 * i - 1) * (float)(2 * i - 1)
                     / ((float)(2 * i) * (float)(2 * i + 1));
        sum += term;
    }
    return 1.5707963f - sum;
}
static inline float nefu_fabsf(float x) {
    return x < 0.0f ? -x : x;
}
#define STBTT_ifloor(x)  ((int)(x))
#define STBTT_iceil(x)   ((int)((x) + 1.0f))
#define STBTT_sqrt(x)    nefu_sqrtf(x)
#define STBTT_pow(x, y)  nefu_powf((x), (y))
#define STBTT_fmod(x, y) nefu_fmodf((x), (y))
#define STBTT_cos(x)     nefu_cosf(x)
#define STBTT_acos(x)    nefu_acosf(x)
#define STBTT_fabs(x)    nefu_fabsf(x)
// stb_truetype.h calls STBTT_assert() in its implementation; the bare kernel
// has no libc assert, so define the hook to compile the checks out (this also
// prevents stb from pulling in <assert.h>, which drags in libstdc++'s cmath
// and breaks under -ffreestanding).
#define STBTT_assert(x) ((void)0)
#endif

// ---- platform memory hooks for stb_truetype ----
#ifdef NEFU_BARE
#define STBTT_malloc(x, u) ((void)(u), nefu::kalloc((size_t)(x)))
#define STBTT_free(x, u)   ((void)(u), nefu::kfree((x)))
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace nefu {

static stbtt_fontinfo g_font;
static bool g_ok = false;

bool ttf_init() {
    if (g_ok) return true;
    g_ok = stbtt_InitFont(&g_font, MONT_TTF_DATA, 0) != 0;
    return g_ok;
}
bool ttf_ready() { return g_ok; }

int ttf_text_width(const char* s, int size) {
    if (!g_ok || !s) return 0;
    float scale = stbtt_ScaleForPixelHeight(&g_font, (float)size);
    int w = 0;
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n') continue;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font, c, &adv, &lsb);
        w += (int)(adv * scale);
    }
    return w;
}

static void blit_glyph(Surface& fb, int x, int y, const unsigned char* bmp,
                       int pw, int ph, unsigned int fg, unsigned int bg) {
    for (int py = 0; py < ph; py++) {
        int fy = y + py;
        if (fy < 0 || fy >= fb.height) continue;
        for (int px = 0; px < pw; px++) {
            int fx = x + px;
            if (fx < 0 || fx >= fb.width) continue;
            unsigned int a = bmp[(size_t)py * (size_t)pw + (size_t)px];
            if (a == 0) continue;
            if (a == 255) {
                fb.setpx(fx, fy, fg);
            } else {
                // simple alpha blend against fg/bg
                unsigned int r = ((fg >> 16) & 0xFF), g = ((fg >> 8) & 0xFF), b = (fg & 0xFF);
                unsigned int br = ((bg >> 16) & 0xFF), bg2 = ((bg >> 8) & 0xFF), bb = (bg & 0xFF);
                r = (r * a + br * (255 - a)) / 255;
                g = (g * a + bg2 * (255 - a)) / 255;
                b = (b * a + bb * (255 - a)) / 255;
                fb.setpx(fx, fy, (0xFF000000u) | (r << 16) | (g << 8) | b);
            }
        }
    }
}

void ttf_draw_text(void* fb_surface, int x, int y, const char* s, int size,
                   unsigned int fg, unsigned int bg) {
    if (!g_ok || !fb_surface || !s) return;
    Surface& fb = *(Surface*)fb_surface;
    float scale = stbtt_ScaleForPixelHeight(&g_font, (float)size);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&g_font, &ascent, &descent, &lineGap);
    int line_h = (int)((ascent - descent + lineGap) * scale);
    if (line_h < size) line_h = size + 2;
    int cx = x;
    int top = y;
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\n') {
            top += line_h;
            cx = x;
            continue;
        }
        if (c < 32) continue;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&g_font, c, &adv, &lsb);
        int pw = 0, ph = 0, ox = 0, oy = 0;
        unsigned char* bmp = stbtt_GetCodepointBitmap(&g_font, scale, scale, c, &pw, &ph, &ox, &oy);
        if (bmp) {
            blit_glyph(fb, cx + ox, top + (int)(ascent * scale) + oy, bmp, pw, ph, fg, bg);
            STBTT_free(bmp, 0);
        }
        cx += (int)(adv * scale);
    }
}

void ttf_draw_wrapped(void* fb_surface, int x, int y, const char* s, int size,
                      unsigned int fg, unsigned int bg, int max_width, int* out_y) {
    if (!g_ok || !fb_surface || !s) return;
    // naive word wrapper: breaks on spaces when a word would overflow
    const char* line_start = s;
    const char* p = s;
    while (1) {
        if (*p == '\n' || *p == 0) {
            ttf_draw_text(fb_surface, x, y, line_start, size, fg, bg);
            y += (int)(size * 1.35f);
            if (*p == 0) break;
            line_start = p + 1;
            p++;
            continue;
        }
        // find next space
        const char* sp = p;
        while (*sp && *sp != ' ' && *sp != '\n') sp++;
        int wid = ttf_text_width(line_start, size);
        int word_end = (sp - line_start);
        // measure up to the space
        char tmp[256];
        int tn = word_end < 255 ? word_end : 255;
        for (int k = 0; k < tn; k++) tmp[k] = line_start[k];
        tmp[tn] = 0;
        int w2 = ttf_text_width(tmp, size);
        if (w2 > max_width && sp > line_start) {
            // wrap before this word
            int wrap_len = (int)(sp - line_start);
            if (wrap_len > 0 && wrap_len < 255) {
                for (int k = 0; k < wrap_len; k++) tmp[k] = line_start[k];
                tmp[wrap_len] = 0;
                ttf_draw_text(fb_surface, x, y, tmp, size, fg, bg);
                y += (int)(size * 1.35f);
                line_start = sp + 1;
                p = sp + 1;
                continue;
            }
        }
        if (*sp == ' ') {
            // measure with the space
            int sp_len = (int)(sp + 1 - line_start);
            if (sp_len < 255) {
                for (int k = 0; k < sp_len; k++) tmp[k] = line_start[k];
                tmp[sp_len] = 0;
                int w3 = ttf_text_width(tmp, size);
                if (w3 > max_width) {
                    ttf_draw_text(fb_surface, x, y, tmp, size, fg, bg);
                    y += (int)(size * 1.35f);
                    line_start = sp + 1;
                    p = sp + 1;
                    continue;
                }
            }
        }
        if (*sp == 0) break;
        p = sp;
        if (*p) p++;
    }
    if (out_y) *out_y = y;
}

} // namespace nefu
