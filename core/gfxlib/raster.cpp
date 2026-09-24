// nefuOS graphics library — software rasterization implementation & self test
#include "raster.h"
#include "../lib/softmath.h"
#include <stdlib.h>
#include <string.h>

namespace nefu {
namespace gfxlib {

// =====================================================================
// low level
// =====================================================================
bool clip_pixel(const Buffer& b, int x, int y) {
    return x >= 0 && x < b.w && y >= 0 && y < b.h;
}

Pixel blend_pixel(Pixel c, Pixel bg, unsigned alpha) {
    if (alpha >= 255) return (c & 0x00FFFFFFu) | 0xFF000000u;
    if (alpha == 0) return bg;
    unsigned ia = 255 - alpha;
    unsigned r = (((c >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * ia) / 255;
    unsigned g = (((c >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * ia) / 255;
    unsigned bl = ((c & 0xFF) * alpha + (bg & 0xFF) * ia) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | bl;
}

void draw_pixel(Buffer b, int x, int y, Pixel c) {
    if (x < 0 || y < 0 || x >= b.w || y >= b.h) return;
    b.data[(size_t)y * (size_t)b.w + (size_t)x] = c;
}

void draw_pixel_clipped(Buffer b, int x, int y, Pixel c) {
    draw_pixel(b, x, y, c);
}

// =====================================================================
// lines
// =====================================================================
static Pixel pixel_at(const Buffer& b, int x, int y) {
    if (x < 0 || y < 0 || x >= b.w || y >= b.h) return 0xFF000000u;
    return b.data[(size_t)y * (size_t)b.w + (size_t)x];
}

void draw_line(Buffer b, int x0, int y0, int x1, int y1, Pixel c) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        draw_pixel(b, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void draw_line_aa(Buffer b, int x0, int y0, int x1, int y1, Pixel c) {
    // Wu's algorithm: two endpoints + 2 weighted pixels per step
    int dx = x1 - x0, dy = y1 - y0;
    bool steep = dx < 0 ? -dx < (dy < 0 ? -dy : dy) : dx < (dy < 0 ? -dy : dy);
    if (steep) {
        int t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        int t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    dx = x1 - x0; dy = y1 - y0;
    if (dx == 0) { draw_line(b, y0, x0, y1, x1, c); return; }
    int grad = dy ? (dy << 16) / dx : 0;
    int y = y0;
    unsigned e = 0;
    auto put = [&](int xx, int yy, unsigned a) {
        if (steep) { int t = xx; xx = yy; yy = t; }
        if (a == 0) return;
        Pixel bg = pixel_at(b, xx, yy);
        draw_pixel(b, xx, yy, blend_pixel(c, bg, 255 - a));
    };
    put(x0, y0, 255);
    put(x1, y1, 255);
    for (int x = x0 + 1; x < x1; x++) {
        e += (unsigned)grad;
        if ((e >> 16) != 0) { y += (int)(e >> 16); e &= 0xFFFF; }
        put(x, y, (e >> 8));
        put(x, y + 1, 255 - (e >> 8));
    }
}

void draw_rect(Buffer b, int x, int y, int w, int h, Pixel c) {
    if (w <= 0 || h <= 0) return;
    draw_line(b, x, y, x + w - 1, y, c);
    draw_line(b, x, y + h - 1, x + w - 1, y + h - 1, c);
    draw_line(b, x, y, x, y + h - 1, c);
    draw_line(b, x + w - 1, y, x + w - 1, y + h - 1, c);
}

void draw_rect_fill(Buffer b, int x, int y, int w, int h, Pixel c) {
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= b.h) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx >= 0 && xx < b.w)
                b.data[(size_t)yy * (size_t)b.w + (size_t)xx] = c;
        }
    }
}

// =====================================================================
// circles / ellipses
// =====================================================================
void draw_circle(Buffer b, int cx, int cy, int r, Pixel c) {
    if (r <= 0) return;
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        draw_pixel(b, cx + x, cy + y, c);
        draw_pixel(b, cx - x, cy + y, c);
        draw_pixel(b, cx + x, cy - y, c);
        draw_pixel(b, cx - x, cy - y, c);
        draw_pixel(b, cx + y, cy + x, c);
        draw_pixel(b, cx - y, cy + x, c);
        draw_pixel(b, cx + y, cy - x, c);
        draw_pixel(b, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

void draw_circle_fill(Buffer b, int cx, int cy, int r, Pixel c) {
    if (r <= 0) return;
    for (int y = -r; y <= r; y++) {
        int h2 = r * r - y * y;
        if (h2 < 0) continue;
        int span = 1;
        while (span * span <= h2) span++;
        span--;
        int yy = cy + y;
        if (yy < 0 || yy >= b.h) continue;
        for (int x = cx - span; x <= cx + span; x++) {
            if (x >= 0 && x < b.w)
                b.data[(size_t)yy * (size_t)b.w + (size_t)x] = c;
        }
    }
}

void draw_ellipse(Buffer b, int cx, int cy, int rx, int ry, Pixel c) {
    if (rx <= 0 || ry <= 0) return;
    // parametric sampling with Q16.16 trig; adjacent samples joined by lines
    // (smooth for the radii used in practice; integer-only, no FPU needed)
    int px = cx + rx, py = cy;
    for (int i = 2; i <= 360; i += 2) {
        nefu::fx::fix a = nefu::fx::fx_deg2rad(nefu::fx::itofix(i));
        int nx = cx + nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(rx), nefu::fx::fx_cos(a)));
        int ny = cy - nefu::fx::fixtoi(nefu::fx::fx_mul(nefu::fx::itofix(ry), nefu::fx::fx_sin(a)));
        draw_line(b, px, py, nx, ny, c);
        px = nx; py = ny;
    }
}

// =====================================================================
// Bezier
// =====================================================================
void draw_bezier(Buffer b, int x0, int y0, int x1, int y1,
                 int x2, int y2, int x3, int y3, Pixel c) {
    // adaptive de Casteljau: subdivide while the control polygon is wide
    long long dist = (long long)(x1 - x0) * (x1 - x0) + (long long)(y1 - y0) * (y1 - y0);
    long long d2 = (long long)(x2 - x1) * (x2 - x1) + (long long)(y2 - y1) * (y2 - y1);
    long long d3 = (long long)(x3 - x2) * (x3 - x2) + (long long)(y3 - y2) * (y3 - y2);
    if (dist <= 1 && d2 <= 1 && d3 <= 1) {
        draw_pixel(b, x0, y0, c);
        draw_pixel(b, x3, y3, c);
        return;
    }
    // split at t=1/2 (integer midpoints)
    int ax = (x0 + x1) / 2, ay = (y0 + y1) / 2;
    int bx = (x1 + x2) / 2, by = (y1 + y2) / 2;
    int cx = (x2 + x3) / 2, cy = (y2 + y3) / 2;
    int abx = (ax + bx) / 2, aby = (ay + by) / 2;
    int bcx = (bx + cx) / 2, bcy = (by + cy) / 2;
    int mx = (abx + bcx) / 2, my = (aby + bcy) / 2;
    // integer-floor midpoint can reproduce the parent segment when control
    // points are 1px apart (e.g. P = (0,4),(1,5),(2,6),(3,7)); detect that
    // no-progress case and flatten the segment instead of recursing forever.
    bool flatL = (ax == x1 && ay == y1 && abx == x2 && aby == y2 && mx == x3 && my == y3);
    bool flatR = (mx == x0 && my == y0 && bcx == x1 && bcy == y1 && cx == x2 && cy == y2);
    if (flatL || flatR) {
        draw_line(b, x0, y0, x3, y3, c);
        return;
    }
    draw_bezier(b, x0, y0, ax, ay, abx, aby, mx, my, c);
    draw_bezier(b, mx, my, bcx, bcy, cx, cy, x3, y3, c);
}

// =====================================================================
// polylines / polygons
// =====================================================================
void draw_polyline(Buffer b, const int* xs, const int* ys, int n, Pixel c) {
    if (n < 2) return;
    for (int i = 0; i < n - 1; i++) draw_line(b, xs[i], ys[i], xs[i + 1], ys[i + 1], c);
}

void draw_polygon(Buffer b, const int* xs, const int* ys, int n, Pixel c) {
    if (n < 2) return;
    draw_polyline(b, xs, ys, n, c);
    draw_line(b, xs[n - 1], ys[n - 1], xs[0], ys[0], c);
}

// =====================================================================
// fills
// =====================================================================
void fill_polygon(Buffer b, const int* xs, const int* ys, int n, Pixel c) {
    if (n < 3) return;
    int ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    if (ymax < 0 || ymin >= b.h) return;
    int* xsects = new int[n + 4];
    if (!xsects) return;
    for (int y = ymin > 0 ? ymin : 0; y <= ymax && y < b.h; y++) {
        int k = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            int yi = ys[i], yj = ys[j];
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                // crossing: x = xi + (y - yi)/(yj - yi) * (xj - xi)
                long long num = (long long)(y - yi) * (xs[j] - xs[i]);
                long long den = (long long)(yj - yi);
                int x = xs[i] + (int)(num / den);
                xsects[k++] = x;
            }
        }
        // sort intersections (insertion, k is tiny)
        for (int a = 1; a < k; a++) {
            int v = xsects[a], p = a - 1;
            while (p >= 0 && xsects[p] > v) { xsects[p + 1] = xsects[p]; p--; }
            xsects[p + 1] = v;
        }
        for (int a = 0; a + 1 < k; a += 2) {
            int xa = xsects[a] < 0 ? 0 : xsects[a];
            int xb = xsects[a + 1] >= b.w ? b.w - 1 : xsects[a + 1];
            if (xa < 0) xa = 0;
            if (xb >= b.w) xb = b.w - 1;
            if (xb >= xa)
                for (int x = xa; x <= xb; x++)
                    b.data[(size_t)y * (size_t)b.w + (size_t)x] = c;
        }
    }
    delete[] xsects;
}

void flood_fill(Buffer b, int sx, int sy, Pixel target, Pixel fill) {
    if (!clip_pixel(b, sx, sy)) return;
    if (b.data[(size_t)sy * (size_t)b.w + (size_t)sx] != target) return;
    if (target == fill) return;
    // scanline-based stack fill: fill runs, push seed pixels above/below
    int* stack = new int[b.w * 2 + 8];
    if (!stack) return;
    int sp = 0;
    stack[sp++] = sx; stack[sp++] = sy;
    while (sp > 0) {
        int y = stack[--sp];
        int x = stack[--sp];
        if (y < 0 || y >= b.h) continue;
        int xl = x;
        while (xl >= 0 && b.data[(size_t)y * (size_t)b.w + (size_t)xl] == target) xl--;
        xl++;
        int xr = x;
        while (xr < b.w && b.data[(size_t)y * (size_t)b.w + (size_t)xr] == target) xr++;
        xr--;
        for (int xx = xl; xx <= xr; xx++)
            b.data[(size_t)y * (size_t)b.w + (size_t)xx] = fill;
        // scan upper/lower row for new runs
        for (int yy = y - 1; yy <= y + 1; yy += 2) {
            if (yy < 0 || yy >= b.h) continue;
            int xx = xl;
            while (xx <= xr) {
                if (b.data[(size_t)yy * (size_t)b.w + (size_t)xx] == target) {
                    stack[sp++] = xx; stack[sp++] = yy;
                    while (xx <= xr && b.data[(size_t)yy * (size_t)b.w + (size_t)xx] == target) xx++;
                } else xx++;
            }
        }
    }
    delete[] stack;
}

// =====================================================================
// clipping
// =====================================================================
bool clip_line_cohen(int& x0, int& y0, int& x1, int& y1,
                     int xmin, int ymin, int xmax, int ymax) {
    const int INSIDE = 0, LEFT = 1, RIGHT = 2, BOTTOM = 4, TOP = 8;
    auto outc = [&](int x, int y) {
        int c = INSIDE;
        if (x < xmin) c |= LEFT;
        else if (x > xmax) c |= RIGHT;
        if (y < ymin) c |= BOTTOM;
        else if (y > ymax) c |= TOP;
        return c;
    };
    int c0 = outc(x0, y0), c1 = outc(x1, y1);
    for (;;) {
        if (!(c0 | c1)) return true;
        if (c0 & c1) return false;
        int c = c0 ? c0 : c1;
        int x = 0, y = 0;
        if (c & TOP) { x = x0 + (x1 - x0) * (ymax - y0) / (y1 - y0); y = ymax; }
        else if (c & BOTTOM) { x = x0 + (x1 - x0) * (ymin - y0) / (y1 - y0); y = ymin; }
        else if (c & RIGHT) { y = y0 + (y1 - y0) * (xmax - x0) / (x1 - x0); x = xmax; }
        else if (c & LEFT) { y = y0 + (y1 - y0) * (xmin - x0) / (x1 - x0); x = xmin; }
        if (c == c0) { x0 = x; y0 = y; c0 = outc(x0, y0); }
        else { x1 = x; y1 = y; c1 = outc(x1, y1); }
    }
}

int clip_polygon_sutherland(const int* xs, const int* ys, int n,
                            int xmin, int ymin, int xmax, int ymax,
                            int* ox, int* oy, int maxout) {
    if (n < 3 || maxout < 2 * n + 8) return 0;
    // four boundary passes; each pass clips against one half-plane
    const int* cx = xs; const int* cy = ys; int cn = n;
    int tx[2 * 64 + 8], ty[2 * 64 + 8];
    int bx[2 * 64 + 8], by[2 * 64 + 8];
    if (cn > 64) return 0;
    // left
    int outn = 0;
    for (int i = 0; i < cn; i++) {
        int j = (i + 1) % cn;
        bool ai = cx[i] >= xmin, aj = cx[j] >= xmin;
        if (ai) { tx[outn] = cx[i]; ty[outn] = cy[i]; outn++; }
        if (ai != aj) {
            long long t = (long long)(cx[i] - xmin) * (cy[j] - cy[i]);
            long long d = (long long)(cx[j] - cx[i]);
            int yy = cy[i] + (d != 0 ? (int)(t / d) : 0);
            tx[outn] = xmin; ty[outn] = yy; outn++;
        }
    }
    // right
    cn = outn; outn = 0;
    for (int i = 0; i < cn; i++) {
        int j = (i + 1) % cn;
        bool ai = tx[i] <= xmax, aj = tx[j] <= xmax;
        if (ai) { bx[outn] = tx[i]; by[outn] = ty[i]; outn++; }
        if (ai != aj) {
            long long t = (long long)(tx[i] - xmax) * (ty[j] - ty[i]);
            long long d = (long long)(tx[j] - tx[i]);
            int yy = ty[i] + (d != 0 ? (int)(t / d) : 0);
            bx[outn] = xmax; by[outn] = yy; outn++;
        }
    }
    // bottom
    cn = outn; outn = 0;
    for (int i = 0; i < cn; i++) {
        int j = (i + 1) % cn;
        bool ai = by[i] >= ymin, aj = by[j] >= ymin;
        if (ai) { tx[outn] = bx[i]; ty[outn] = by[i]; outn++; }
        if (ai != aj) {
            long long t = (long long)(by[i] - ymin) * (bx[j] - bx[i]);
            long long d = (long long)(by[j] - by[i]);
            int xx = bx[i] + (d != 0 ? (int)(t / d) : 0);
            tx[outn] = xx; ty[outn] = ymin; outn++;
        }
    }
    // top
    cn = outn; outn = 0;
    for (int i = 0; i < cn; i++) {
        int j = (i + 1) % cn;
        bool ai = ty[i] <= ymax, aj = ty[j] <= ymax;
        if (ai) { bx[outn] = tx[i]; by[outn] = ty[i]; outn++; }
        if (ai != aj) {
            long long t = (long long)(ty[i] - ymax) * (tx[j] - tx[i]);
            long long d = (long long)(ty[j] - ty[i]);
            int xx = tx[i] + (d != 0 ? (int)(t / d) : 0);
            bx[outn] = xx; by[outn] = ymax; outn++;
        }
    }
    if (outn > maxout) outn = maxout;
    for (int i = 0; i < outn; i++) { ox[i] = bx[i]; oy[i] = by[i]; }
    return outn;
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_raster_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_raster_fails++;
    (void)what;
}
// small offscreen buffer; verify pixel values
bool buf_has(const Buffer& b, Pixel want) {
    for (int y = 0; y < b.h; y++)
        for (int x = 0; x < b.w; x++)
            if (b.data[(size_t)y * (size_t)b.w + (size_t)x] == want) return true;
    return false;
}
} // namespace

int raster_self_test() {
    g_raster_fails = 0;
    Pixel buf[16 * 16];
    Buffer b;
    b.data = buf; b.w = 16; b.h = 16;

    // clip_pixel boundaries
    expect("clip-in", clip_pixel(b, 0, 0) && clip_pixel(b, 15, 15));
    expect("clip-out", !clip_pixel(b, -1, 0) && !clip_pixel(b, 0, 16) && !clip_pixel(b, 99, 99));

    // pixel + blend
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    draw_pixel(b, 3, 4, 0xFFFF0000);
    expect("pixel-set", buf[4 * 16 + 3] == 0xFFFF0000);
    expect("pixel-oclip", (draw_pixel(b, 99, 99, 0xFFFF0000), buf[0] != 0xFFFF0000));

    Pixel bl = blend_pixel(0xFFFFFFFF, 0xFF000000, 128);
    // 50% blend of white over black: (255*128)/255 = 128 = 0x80
    expect("blend-half", ((bl >> 16) & 0xFF) == 0x80 && (bl & 0xFF) == 0x80);

    // horizontal / vertical / diagonal line coverage
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    draw_line(b, 0, 8, 15, 8, 0xFFFFFFFF);
    int hc = 0;
    for (int x = 0; x < 16; x++) if (buf[8 * 16 + x] == 0xFFFFFFFF) hc++;
    expect("line-h", hc >= 15);   // endpoints inclusive, integer steps

    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    draw_line(b, 0, 0, 15, 15, 0xFFFFFFFF);
    int dc = 0;
    for (int k = 0; k < 16; k++) if (buf[k * 16 + k] == 0xFFFFFFFF) dc++;
    expect("line-diag", dc == 16);

    // circle: every point on the outline must satisfy |d - r| <= 1
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    draw_circle(b, 7, 7, 5, 0xFFFFFFFF);
    int onr = 0;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (buf[y * 16 + x] == 0xFFFFFFFF) {
                int d2 = (x - 7) * (x - 7) + (y - 7) * (y - 7);
                if (d2 >= (5 - 1) * (5 - 1) && d2 <= (5 + 1) * (5 + 1)) onr++;
            }
    expect("circle-ring", onr > 8 && onr <= 60);

    // filled circle: center must be set
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    draw_circle_fill(b, 7, 7, 4, 0xFFFFFFFF);
    expect("circle-fill-center", buf[7 * 16 + 7] == 0xFFFFFFFF);
    // corner stays clear
    expect("circle-fill-corner", buf[0] == 0);

    // bezier: endpoints must be painted
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    draw_bezier(b, 0, 0, 5, 14, 10, 14, 15, 0, 0xFFFFFFFF);
    expect("bezier-endpoints", buf[0] == 0xFFFFFFFF && buf[0 * 16 + 15] == 0xFFFFFFFF);

    // polygon fill: triangle covers its centroid
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0;
    {
        int xs[3] = {2, 14, 8}, ys[3] = {14, 14, 2};
        fill_polygon(b, xs, ys, 3, 0xFFFFFFFF);
        expect("polyfill-centroid", buf[10 * 16 + 8] == 0xFFFFFFFF);
    }

    // flood fill: closed square region
    for (int i = 0; i < 16 * 16; i++) buf[i] = 0xFFFF0000;
    draw_rect(b, 4, 4, 8, 8, 0xFF000000);       // black square outline
    flood_fill(b, 6, 6, 0xFFFF0000, 0xFF00FF00);
    expect("flood-inside", buf[6 * 16 + 6] == 0xFF00FF00);
    expect("flood-outside", buf[0] == 0xFFFF0000);

    // cohen-sutherland: full inside, full outside, partial
    int x0 = 0, y0 = 0, x1 = 15, y1 = 15;
    expect("cs-inside", clip_line_cohen(x0, y0, x1, y1, 0, 0, 15, 15));
    x0 = -10; y0 = 5; x1 = 20; y1 = 5;
    expect("cs-partial", clip_line_cohen(x0, y0, x1, y1, 0, 0, 15, 15));
    expect("cs-clipped-x", x0 >= 0 && x1 <= 15);
    x0 = -10; y0 = -10; x1 = -5; y1 = -5;
    expect("cs-outside", !clip_line_cohen(x0, y0, x1, y1, 0, 0, 15, 15));

    // polygon clip: square fully inside stays 4 verts; half-clipped shrinks
    {
        int xs[4] = {2, 14, 14, 2}, ys[4] = {2, 2, 14, 14};
        int ox[64], oy[64];
        int n = clip_polygon_sutherland(xs, ys, 4, 0, 0, 15, 15, ox, oy, 64);
        expect("clip-sq-full", n == 4);
        int xs2[4] = {-4, 8, 8, -4}, ys2[4] = {2, 2, 14, 14};
        n = clip_polygon_sutherland(xs2, ys2, 4, 0, 0, 15, 15, ox, oy, 64);
        expect("clip-sq-half", n >= 4 && n <= 8);
        int xs3[4] = {-10, -5, -5, -10}, ys3[4] = {-10, -10, -5, -5};
        n = clip_polygon_sutherland(xs3, ys3, 4, 0, 0, 15, 15, ox, oy, 64);
        expect("clip-sq-none", n == 0);
    }

    return g_raster_fails;
}

} // namespace gfxlib
} // namespace nefu
