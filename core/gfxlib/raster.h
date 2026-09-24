// nefuOS graphics library — software rasterization (integer only)
// API contract: pixel buffers are 0xAARRGGBB, coordinates are integers,
// all algorithms are integer-only so the bare kernel (no FPU) can use them.
// See raster.cpp for the self test.
#pragma once
#include <stdint.h>

namespace nefu {
namespace gfxlib {

typedef uint32_t Pixel;              // 0xAARRGGBB

struct Buffer {                      // raw pixel target
    Pixel* data;
    int w, h;
};

// clip helper: returns false when the pixel lies outside [0,w)x[0,h)
bool clip_pixel(const Buffer& b, int x, int y);

// blend: c over bg with 8-bit alpha (c.a = 0xFF for opaque)
Pixel blend_pixel(Pixel c, Pixel bg, unsigned alpha);

void draw_pixel(Buffer b, int x, int y, Pixel c);
void draw_pixel_clipped(Buffer b, int x, int y, Pixel c);

// --- primitives ---
// Bresenham line (integer, exact endpoints)
void draw_line(Buffer b, int x0, int y0, int x1, int y1, Pixel c);
// Wu anti-aliased line (4 weighted pixels per step, blends into background)
void draw_line_aa(Buffer b, int x0, int y0, int x1, int y1, Pixel c);
void draw_rect(Buffer b, int x, int y, int w, int h, Pixel c);
void draw_rect_fill(Buffer b, int x, int y, int w, int h, Pixel c);
// midpoint circle / filled circle
void draw_circle(Buffer b, int cx, int cy, int r, Pixel c);
void draw_circle_fill(Buffer b, int cx, int cy, int r, Pixel c);
// midpoint ellipse (axis aligned)
void draw_ellipse(Buffer b, int cx, int cy, int rx, int ry, Pixel c);
// cubic Bezier via de Casteljau subdivision (min segment length 1px)
void draw_bezier(Buffer b, int x0, int y0, int x1, int y1,
                 int x2, int y2, int x3, int y3, Pixel c);
// polyline / polygon outline (closed)
void draw_polyline(Buffer b, const int* xs, const int* ys, int n, Pixel c);
void draw_polygon(Buffer b, const int* xs, const int* ys, int n, Pixel c);

// --- fills ---
// scanline polygon fill (even-odd rule, integer edges, self-intersection safe)
void fill_polygon(Buffer b, const int* xs, const int* ys, int n, Pixel c);
// flood fill from (sx,sy) replacing color 'target' (scanline-ish stack fill)
void flood_fill(Buffer b, int sx, int sy, Pixel target, Pixel fill);

// --- clipping ---
// Cohen-Sutherland line clip against [xmin,xmax]x[ymin,ymax]; returns false
// when the segment is fully outside.
bool clip_line_cohen(int& x0, int& y0, int& x1, int& y1,
                     int xmin, int ymin, int xmax, int ymax);
// Sutherland-Hodgman polygon clip (clip edges one boundary at a time).
// Returns the number of output vertices (0 when fully clipped); out arrays
// must hold at least 2*n+8 entries.
int clip_polygon_sutherland(const int* xs, const int* ys, int n,
                            int xmin, int ymin, int xmax, int ymax,
                            int* ox, int* oy, int maxout);

// --- self test ---
// runs every assertion; returns the number of failures (0 = all pass)
int raster_self_test();

} // namespace gfxlib
} // namespace nefu
