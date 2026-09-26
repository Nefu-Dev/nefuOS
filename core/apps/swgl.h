// nefuOS software WebGL2 engine — bare-metal renderer + shared protocol helpers.
// The browser's JS bridge encodes every GL call as "\x01gl<cmd>|cid|args"
// control lines; on the Win32 host they run on a real desktop GL context (WGL,
// iGPU hardware acceleration), on bare metal they are decoded here and rendered
// by a self-contained software rasterizer (framebuffer + depth, GLSL ES 3.00
// subset interpreter, textures, blending, culling, scissor, instancing).
//
// Constraints (bare kernel): compiled with -mgeneral-regs-only, so ONLY single
// precision float arithmetic is available (kernel soft-float library). Never
// use double, STL, or any libc beyond the nefu::k* functions below.
#pragma once
#include "../klib/klib.h"
#include "../platform.h"   // kalloc/kfree declarations (both backends)

namespace nefu {

// ===================== shared protocol helpers (host + bare) =====================

// locale-free float parser ("%g"-style output from the JS bridge). Returns the
// parsed value, or 0.0f for empty/partial input.
inline float sw_atof(const char* s) {
    if (!s) return 0.0f;
    while (*s == ' ' || *s == '\t') s++;
    bool neg = false;
    if (*s == '-' || *s == '+') { neg = (*s == '-'); s++; }
    float v = 0.0f;
    while (*s >= '0' && *s <= '9') { v = v * 10.0f + (float)(*s - '0'); s++; }
    float frac = 1.0f;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { frac *= 0.1f; v += (float)(*s - '0') * frac; s++; }
    }
    float e = 1.0f; int ee = 0; bool eneg = false;
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-' || *s == '+') { eneg = (*s == '-'); s++; }
        while (*s >= '0' && *s <= '9') { ee = ee * 10 + (*s - '0'); s++; }
        while (ee-- > 0) e *= 10.0f;
        if (eneg) e = 1.0f / e;
    }
    v *= e;
    return neg ? -v : v;
}

// base64 decode (RFC 4648). Returns bytes written (<= cap). Handles padding.
inline int sw_b64_decode(const char* in, uint8_t* out, int cap) {
    static const signed char T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1, 0,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    int n = 0, acc = 0, bits = 0;
    for (const char* p = in; *p && n < cap; p++) {
        if (*p == '=') break;
        signed char d = T[(unsigned char)*p];
        if (d < 0) continue;
        acc = (acc << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[n++] = (uint8_t)((acc >> bits) & 0xFF);
        }
    }
    return n;
}

// in-place URL percent decode (src==dst allowed, out never longer than in).
inline void sw_url_decode(char* s) {
    char* w = s;
    for (const char* r = s; *r; ) {
        if (*r == '%' && r[1] && r[2]) {
            auto hexv = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            *w++ = (char)(hexv(r[1]) * 16 + hexv(r[2]));
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
}

// ===================== software GL (bare metal) =====================

// Dispatch one decoded GL command (cmd without the "gl" prefix; val already
// URL-decoded). Creates the canvas on the first "ctx" command, like the host.
void swgl_cmd(const char* cid, const char* cmd, const char* val);

// Top-down RGBA8 framebuffer of the named canvas (0 if none / not rendered).
// w/h receive the canvas size. The buffer stays owned by swgl until the canvas
// is resized or freed.
const uint8_t* swgl_frame(const char* cid, int* w, int* h);

// Resize a canvas (also called by the JS canvas.width/height setter).
void swgl_resize(const char* cid, int w, int h);

// Release all software GL canvases (browser shutdown).
void swgl_shutdown();

// True once at least one context was created (diagnostics / status bar).
bool swgl_active();

} // namespace nefu
