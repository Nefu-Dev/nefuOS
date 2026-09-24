// nefuOS graphics library — 2D geometry (integer only)
// Points, rects, circles, triangles, segment intersection, polygon
// measures, point-in-polygon and convex hull.  All arithmetic uses
// 64-bit intermediates so 32-bit coordinates never overflow.
#pragma once
#include <stdint.h>

namespace nefu {
namespace gfxlib {

struct Point { int x, y; };
struct Rect  { int x, y, w, h; };
struct Circle { int cx, cy, r; };

// point/rect/circle tests
bool point_in_rect(int px, int py, const Rect& r);
bool point_in_circle(int px, int py, const Circle& c);
bool rect_overlap(const Rect& a, const Rect& b);
bool circle_overlap(const Circle& a, const Circle& b);
bool circle_rect_overlap(const Circle& c, const Rect& r);

// distance squared (64-bit)
long long dist2(int x0, int y0, int x1, int y1);
long long point_segment_dist2(int px, int py, int ax, int ay, int bx, int by);

// orientation of (a,b,c): <0 clockwise, >0 counter-clockwise, 0 collinear
long long orient(const Point& a, const Point& b, const Point& c);
bool point_on_segment(const Point& p, const Point& a, const Point& b);
// proper intersection (not just touching at endpoints)
bool segments_intersect(const Point& a, const Point& b,
                        const Point& c, const Point& d);
// intersection point of two infinite lines; returns false when parallel
bool line_intersection(const Point& a, const Point& b,
                       const Point& c, const Point& d, Point* out);

// triangle helpers
long long triangle_area2(const Point& a, const Point& b, const Point& c); // 2x signed area
bool point_in_triangle(const Point& p, const Point& a, const Point& b, const Point& c);

// polygon helpers (n >= 3); area2 is signed (shoelace)
long long polygon_area2(const Point* p, int n);
Point polygon_centroid(const Point* p, int n);
bool point_in_polygon(const Point& p, const Point* poly, int n);  // ray casting

// convex hull (Andrew monotone chain).  Returns hull size; hull buffer must
// hold at least n points.  Degenerate inputs (<3 points) return 0.
int convex_hull(const Point* pts, int n, Point* hull);

// self test
int geo_self_test();

} // namespace gfxlib
} // namespace nefu
