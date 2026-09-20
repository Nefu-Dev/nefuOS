// nefuOS graphics primitives
#pragma once
#include "../platform.h"
#include "../klib/klib.h"

namespace nefu {

// ：32bpp，memory byte order BGRA; 24bpp supported via bpp flag
// (QEMU's stdvga "24bpp" VBE modes store 24-bit pixels with a 32-bit stride).
struct Surface {
    uint8_t* addr;
    int width, height, pitch;
    int bpp = 32;
    inline void setpx(int x, int y, uint32_t c) {
        if (bpp == 24) {
            // QEMU stdvga 24bpp VBE modes: 4-byte slot per pixel (pitch = w*4),
            // only the first 3 bytes (BGR) are displayed.
            uint8_t* p = addr + (size_t)y * (size_t)pitch + (size_t)x * 4;
            p[0] = (uint8_t)c;
            p[1] = (uint8_t)(c >> 8);
            p[2] = (uint8_t)(c >> 16);
        } else {
            *(uint32_t*)(addr + (size_t)y * (size_t)pitch + (size_t)x * 4) = c;
        }
    }
    inline uint32_t getpx(int x, int y) {
        if (bpp == 24) {
            const uint8_t* p = addr + (size_t)y * (size_t)pitch + (size_t)x * 4;
            return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
        }
        return *(uint32_t*)(addr + (size_t)y * (size_t)pitch + (size_t)x * 4);
    }
    void fill(uint32_t c) {
        for (int y = 0; y < height; y++)
            for (int x = 0; x < width; x++) setpx(x, y, c);
    }
};

namespace color {
const uint32_t BLACK   = 0x00000000;
const uint32_t WHITE   = 0x00FFFFFF;
const uint32_t GRAY    = 0x00808080;
const uint32_t LIGHT   = 0x00D9D9D4;
const uint32_t DARK    = 0x003F3F3C;
const uint32_t BLUE    = 0x002F6FB6;
const uint32_t BLUE_LT = 0x005C9BD6;
const uint32_t GREEN   = 0x0030A14A;
const uint32_t RED     = 0x00D13438;
const uint32_t YELLOW  = 0x00F0C040;
const uint32_t ORANGE  = 0x00F08A24;
const uint32_t TEAL    = 0x0038B2A0;
const uint32_t CREAM   = 0x00F4F3EE;
const uint32_t PANEL   = 0x00ECEBE6;
const uint32_t TEXT    = 0x001A1B1C;
const uint32_t TEXT2   = 0x006B7280;
const uint32_t BORDER  = 0x00C9C8C2;
}

namespace gfx {

void pixel(Surface& s, int x, int y, uint32_t c);
void fillrect(Surface& s, int x, int y, int w, int h, uint32_t c);
void rect(Surface& s, int x, int y, int w, int h, uint32_t c);
void hline(Surface& s, int x0, int x1, int y, uint32_t c);
void vline(Surface& s, int x, int y0, int y1, uint32_t c);
void line(Surface& s, int x0, int y0, int x1, int y1, uint32_t c);
void circle(Surface& s, int cx, int cy, int r, uint32_t c);
void fillcircle(Surface& s, int cx, int cy, int r, uint32_t c);
int sqrti(int v);
void char8x16(Surface& s, int x, int y, char ch, uint32_t fg, uint32_t bg);
void char16x16(Surface& s, int x, int y, uint32_t uc, uint32_t fg, uint32_t bg);
void text(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg);
void text_scale(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg, int scale);
// TrueType text via the platform layer; falls back to the bitmap font when
// the platform cannot render TTF (bare kernel has no font data).
void text_ttf(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg, int px = 18);
void blit(Surface& dst, Surface& src, int dx, int dy);
void blit_clip(Surface& dst, Surface& src, int dx, int dy, int sx, int sy, int w, int h);
int  text_width(const char* s);

} // namespace gfx

inline Surface screen_surface() {
    Screen* sc = platform_screen();
    Surface s;
    s.addr = sc->addr;
    s.width = sc->width;
    s.height = sc->height;
    s.pitch = sc->pitch;
    return s;
}

} // namespace nefu
