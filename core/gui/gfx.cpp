// nefuOS 图形原语实现
#include "gfx.h"
#include "font.h"
#include "font16.h"

namespace nefu {
namespace gfx {

void pixel(Surface& s, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= s.width || y >= s.height) return;
    s.px(x, y) = c;
}

void fillrect(Surface& s, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > s.width) x1 = s.width;
    int y1 = y + h; if (y1 > s.height) y1 = s.height;
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = (uint32_t*)(s.addr + (size_t)yy * (size_t)s.pitch);
        for (int xx = x0; xx < x1; xx++) row[xx] = c;
    }
}

void rect(Surface& s, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    hline(s, x, x + w - 1, y, c);
    hline(s, x, x + w - 1, y + h - 1, c);
    vline(s, x, y, y + h - 1, c);
    vline(s, x + w - 1, y, y + h - 1, c);
}

void hline(Surface& s, int x0, int x1, int y, uint32_t c) {
    if (y < 0 || y >= s.height) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= s.width) x1 = s.width - 1;
    uint32_t* row = (uint32_t*)(s.addr + (size_t)y * (size_t)s.pitch);
    for (int x = x0; x <= x1; x++) row[x] = c;
}

void vline(Surface& s, int x, int y0, int y1, uint32_t c) {
    if (x < 0 || x >= s.width) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= s.height) y1 = s.height - 1;
    for (int y = y0; y <= y1; y++) s.px(x, y) = c;
}

void line(Surface& s, int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void char8x16(Surface& s, int x, int y, char ch, uint32_t fg, uint32_t bg) {
    if (x + 8 < 0 || y + 16 < 0 || x >= s.width || y >= s.height) return;
    int idx = ((unsigned char)ch >= 32 && (unsigned char)ch <= 127) ? ch - 32 : 0;
    for (int row = 0; row < 16; row++) {
        unsigned char b = nefu::font8x16[idx][row];
        if (y + row < 0 || y + row >= s.height) continue;
        for (int col = 0; col < 8; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= s.width) continue;
            s.px(xx, y + row) = (b & (0x80 >> col)) ? fg : bg;
        }
    }
}


// ---- UTF-8 decoding ----
static uint32_t utf8_next(const char*& p) {
    unsigned char c = (unsigned char)*p;
    if (c == 0) return 0;
    if (c < 0x80) { p++; return c; }
    if ((c & 0xE0) == 0xC0 && (unsigned char)p[1]) {
        uint32_t v = ((uint32_t)(c & 0x1F) << 6) | ((unsigned char)p[1] & 0x3F);
        p += 2; return v;
    }
    if ((c & 0xF0) == 0xE0 && (unsigned char)p[1] && (unsigned char)p[2]) {
        uint32_t v = ((uint32_t)(c & 0x0F) << 12) | (((unsigned char)p[1] & 0x3F) << 6) | ((unsigned char)p[2] & 0x3F);
        p += 3; return v;
    }
    p++; return 0xFFFD;
}

static const uint8_t* glyph16(uint32_t uc) {
    int lo = 0, hi = nefu::font16_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (nefu::font16[mid].uc == uc) return nefu::font16[mid].data;
        if (nefu::font16[mid].uc < uc) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

void char16x16(Surface& s, int x, int y, uint32_t uc, uint32_t fg, uint32_t bg) {
    if (x + 16 < 0 || y + 16 < 0 || x >= s.width || y >= s.height) return;
    const uint8_t* d = glyph16(uc);
    if (!d) { rect(s, x, y, 16, 16, fg); return; }
    for (int row = 0; row < 16; row++) {
        unsigned short b = (unsigned short)((d[row * 2] << 8) | d[row * 2 + 1]);
        if (y + row < 0 || y + row >= s.height) continue;
        for (int col = 0; col < 16; col++) {
            int xx = x + col;
            if (xx < 0 || xx >= s.width) continue;
            s.px(xx, y + row) = (b & (0x8000 >> col)) ? fg : bg;
        }
    }
}

void text(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg) {
    if (!str) return;
    int ox = x;
    while (*str) {
        if (*str == '\n') { y += 16; x = ox; str++; continue; }
        if (*str == '\r') { str++; continue; }
        uint32_t uc = utf8_next(str);
        if (uc < 0x80) { char8x16(s, x, y, (char)uc, fg, bg); x += 8; }
        else if (uc != 0xFFFD) { char16x16(s, x, y, uc, fg, bg); x += 16; }
        else { char8x16(s, x, y, '?', fg, bg); x += 8; }
    }
}

void text_scale(Surface& s, int x, int y, const char* str, uint32_t fg, uint32_t bg, int scale) {
    (void)bg;
    if (!str || scale < 1) return;
    int ox = x;
    while (*str) {
        if (*str == '\n') { y += 16 * scale; x = ox; str++; continue; }
        uint32_t uc = utf8_next(str);
        if (uc < 0x80) {
            int idx = ((unsigned char)uc >= 32 && (unsigned char)uc <= 127) ? (int)uc - 32 : 0;
            for (int row = 0; row < 16; row++) {
                unsigned char b = nefu::font8x16[idx][row];
                for (int r = 0; r < scale; r++)
                    for (int col = 0; col < 8; col++)
                        if (b & (0x80 >> col))
                            fillrect(s, x + col * scale, y + row * scale + r, scale, 1, fg);
            }
            x += 8 * scale;
        } else if (uc != 0xFFFD) {
            const uint8_t* d = glyph16(uc);
            if (d) {
                for (int row = 0; row < 16; row++) {
                    unsigned short b = (unsigned short)((d[row * 2] << 8) | d[row * 2 + 1]);
                    for (int r = 0; r < scale; r++)
                        for (int col = 0; col < 16; col++)
                            if (b & (0x8000 >> col))
                                fillrect(s, x + col * scale, y + row * scale + r, scale, 1, fg);
                }
            }
            x += 16 * scale;
        } else {
            x += 8 * scale;
        }
    }
}

void blit(Surface& dst, Surface& src, int dx, int dy) {
    blit_clip(dst, src, dx, dy, 0, 0, src.width, src.height);
}

void blit_clip(Surface& dst, Surface& src, int dx, int dy, int sx, int sy, int w, int h) {
    if (w <= 0 || h <= 0) return;
    // 源裁剪
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src.width) w = src.width - sx;
    if (sy + h > src.height) h = src.height - sy;
    // 目标裁剪
    if (dx < 0) { w += dx; sx -= dx; dx = 0; }
    if (dy < 0) { h += dy; sy -= dy; dy = 0; }
    if (dx + w > dst.width) w = dst.width - dx;
    if (dy + h > dst.height) h = dst.height - dy;
    if (w <= 0 || h <= 0) return;
    for (int y = 0; y < h; y++) {
        const uint8_t* sp = src.addr + (size_t)(sy + y) * (size_t)src.pitch + (size_t)sx * 4;
        uint8_t* dp = dst.addr + (size_t)(dy + y) * (size_t)dst.pitch + (size_t)dx * 4;
        memcpy(dp, sp, (size_t)w * 4);
    }
}

void circle(Surface& s, int cx, int cy, int r, uint32_t c) {
    if (r < 0) return;
    int x = 0, y = r, d = 3 - 2 * r;
    while (x <= y) {
        pixel(s, cx + x, cy + y, c); pixel(s, cx - x, cy + y, c);
        pixel(s, cx + x, cy - y, c); pixel(s, cx - x, cy - y, c);
        pixel(s, cx + y, cy + x, c); pixel(s, cx - y, cy + x, c);
        pixel(s, cx + y, cy - x, c); pixel(s, cx - y, cy - x, c);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

void fillcircle(Surface& s, int cx, int cy, int r, uint32_t c) {
    if (r < 0) return;
    for (int y = -r; y <= r; y++) {
        int dx = (int)(sqrti((r * r) - (y * y)));
        hline(s, cx - dx, cx + dx, cy + y, c);
    }
}

// 整数平方根（不含浮点）
int sqrti(int v) {
    if (v <= 0) return 0;
    int x = v, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}

int text_width(const char* s) {
    if (!s) return 0;
    int n = 0;
    while (*s) {
        uint32_t uc = utf8_next(s);
        n += (uc < 0x80) ? 8 : 16;
    }
    return n;
}

} // namespace gfx
} // namespace nefu
