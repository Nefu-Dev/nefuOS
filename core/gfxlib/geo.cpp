// nefuOS graphics library — 2D geometry implementation & self test
#include "geo.h"
#include <stdio.h>
#include <stdlib.h>

namespace nefu {
namespace gfxlib {

bool point_in_rect(int px, int py, const Rect& r) {
    return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
}

bool point_in_circle(int px, int py, const Circle& c) {
    long long dx = (long long)px - c.cx, dy = (long long)py - c.cy;
    return dx * dx + dy * dy <= (long long)c.r * c.r;
}

bool rect_overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

bool circle_overlap(const Circle& a, const Circle& b) {
    long long dx = (long long)a.cx - b.cx, dy = (long long)a.cy - b.cy;
    long long rr = (long long)a.r + b.r;
    return dx * dx + dy * dy <= rr * rr;
}

bool circle_rect_overlap(const Circle& c, const Rect& r) {
    long long cx = (long long)c.cx < r.x ? r.x : ((long long)c.cx >= r.x + r.w ? r.x + r.w - 1 : c.cx);
    long long cy = (long long)c.cy < r.y ? r.y : ((long long)c.cy >= r.y + r.h ? r.y + r.h - 1 : c.cy);
    long long dx = (long long)c.cx - cx, dy = (long long)c.cy - cy;
    return dx * dx + dy * dy <= (long long)c.r * c.r;
}

long long dist2(int x0, int y0, int x1, int y1) {
    long long dx = (long long)x1 - x0, dy = (long long)y1 - y0;
    return dx * dx + dy * dy;
}

long long point_segment_dist2(int px, int py, int ax, int ay, int bx, int by) {
    long long abx = (long long)bx - ax, aby = (long long)by - ay;
    long long apx = (long long)px - ax, apy = (long long)py - ay;
    long long len2 = abx * abx + aby * aby;
    if (len2 == 0) return apx * apx + apy * apy;
    long long t = (apx * abx + apy * aby);
    if (t <= 0) return apx * apx + apy * apy;
    if (t >= len2) {
        long long bpx = (long long)px - bx, bpy = (long long)py - by;
        return bpx * bpx + bpy * bpy;
    }
    long long dx = apx * len2 - abx * t;
    long long dy = apy * len2 - aby * t;
    // distance^2 * len2^2; return scaled
    return (dx * dx + dy * dy) / len2;
}

long long orient(const Point& a, const Point& b, const Point& c) {
    return (long long)(b.x - a.x) * (c.y - a.y) - (long long)(b.y - a.y) * (c.x - a.x);
}

bool point_on_segment(const Point& p, const Point& a, const Point& b) {
    if (orient(a, b, p) != 0) return false;
    return p.x >= (a.x < b.x ? a.x : b.x) && p.x <= (a.x > b.x ? a.x : b.x) &&
           p.y >= (a.y < b.y ? a.y : b.y) && p.y <= (a.y > b.y ? a.y : b.y);
}

bool segments_intersect(const Point& a, const Point& b,
                        const Point& c, const Point& d) {
    long long o1 = orient(a, b, c), o2 = orient(a, b, d);
    long long o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) &&
        ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0)))
        return true;
    // collinear touching cases
    if (o1 == 0 && point_on_segment(c, a, b)) return true;
    if (o2 == 0 && point_on_segment(d, a, b)) return true;
    if (o3 == 0 && point_on_segment(a, c, d)) return true;
    if (o4 == 0 && point_on_segment(b, c, d)) return true;
    return false;
}

bool line_intersection(const Point& a, const Point& b,
                       const Point& c, const Point& d, Point* out) {
    long long abx = (long long)b.x - a.x, aby = (long long)b.y - a.y;
    long long cdx = (long long)d.x - c.x, cdy = (long long)d.y - c.y;
    long long den = abx * cdy - aby * cdx;
    if (den == 0) return false;
    long long acx = (long long)c.x - a.x, acy = (long long)c.y - a.y;
    long long t = (acx * cdy - acy * cdx);
    // x = a.x + abx * t / den
    out->x = a.x + (int)(abx * t / den);
    out->y = a.y + (int)(aby * t / den);
    return true;
}

long long triangle_area2(const Point& a, const Point& b, const Point& c) {
    return orient(a, b, c);
}

bool point_in_triangle(const Point& p, const Point& a, const Point& b, const Point& c) {
    long long d1 = orient(a, b, p), d2 = orient(b, c, p), d3 = orient(c, a, p);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);   // all same sign (or on an edge)
}

long long polygon_area2(const Point* p, int n) {
    // shoelace double area, ccw-positive: sum (xi*yj - xj*yi), j = i+1
    long long s = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        s += (long long)p[i].x * p[j].y - (long long)p[j].x * p[i].y;
    }
    return s;
}

Point polygon_centroid(const Point* p, int n) {
    Point c = {0, 0};
    long long a2 = polygon_area2(p, n);
    if (a2 == 0) return c;
    long long cx = 0, cy = 0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        long long f = (long long)p[i].x * p[j].y - (long long)p[j].x * p[i].y;
        cx += f * (p[i].x + p[j].x);
        cy += f * (p[i].y + p[j].y);
    }
    c.x = (int)(cx / (3 * a2));
    c.y = (int)(cy / (3 * a2));
    return c;
}

bool point_in_polygon(const Point& p, const Point* poly, int n) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        if (((poly[i].y > p.y) != (poly[j].y > p.y)) &&
            (p.x < (long long)(poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                        (long long)(poly[j].y - poly[i].y) + poly[i].x))
            inside = !inside;
    }
    return inside;
}

int convex_hull(const Point* pts, int n, Point* hull) {
    if (n < 3) return 0;
    // sort by (x, y) — insertion sort is fine for the sizes used here
    Point* s = new Point[n];
    for (int i = 0; i < n; i++) s[i] = pts[i];
    for (int i = 1; i < n; i++) {
        Point v = s[i];
        int j = i - 1;
        while (j >= 0 && (s[j].x > v.x || (s[j].x == v.x && s[j].y > v.y))) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = v;
    }
    int k = 0;
    Point* h = new Point[n + 2];
    // lower hull
    for (int i = 0; i < n; i++) {
        while (k >= 2 && orient(h[k - 2], h[k - 1], s[i]) <= 0) k--;
        h[k++] = s[i];
    }
    // upper hull
    int lower = k + 1;
    for (int i = n - 2; i >= 0; i--) {
        while (k >= lower && orient(h[k - 2], h[k - 1], s[i]) <= 0) k--;
        h[k++] = s[i];
    }
    k--;   // last point duplicates the first
    if (k < 3) { delete[] h; delete[] s; return 0; }
    for (int i = 0; i < k; i++) hull[i] = h[i];
    delete[] h;
    delete[] s;
    return k;
}

// =====================================================================
// self test
// =====================================================================
namespace {
int g_geo_fails = 0;
void expect(const char* what, bool ok) {
    if (!ok) g_geo_fails++;
    (void)what;
}
} // namespace

int geo_self_test() {
    g_geo_fails = 0;

    Rect r = {2, 2, 8, 8};
    expect("p-rect-in", point_in_rect(5, 5, r) && point_in_rect(2, 2, r));
    expect("p-rect-out", !point_in_rect(1, 5, r) && !point_in_rect(10, 10, r));

    Circle c = {10, 10, 5};
    expect("p-circle-in", point_in_circle(12, 12, c));
    expect("p-circle-edge", point_in_circle(15, 10, c));   // on radius
    expect("p-circle-out", !point_in_circle(16, 10, c));

    Rect a = {0, 0, 5, 5}, b = {4, 4, 5, 5}, far = {20, 20, 2, 2};
    expect("rect-overlap", rect_overlap(a, b) && rect_overlap(b, a));
    expect("rect-disjoint", !rect_overlap(a, far));

    Circle c2 = {14, 10, 4};
    expect("circle-overlap", circle_overlap(c, c2));
    Circle c3 = {40, 40, 3};
    expect("circle-disjoint", !circle_overlap(c, c3));

    expect("circle-rect", circle_rect_overlap(c, b));
    expect("circle-rect-no", !circle_rect_overlap(c, far));

    expect("dist2", dist2(0, 0, 3, 4) == 25);
    expect("seg-dist-end", point_segment_dist2(0, 0, 2, 2, 8, 2) == 8);
    expect("seg-dist-on", point_segment_dist2(5, 2, 2, 2, 8, 2) == 0);

    // orientation: (0,0),(1,1),(2,0) is clockwise -> negative
    Point pa = {0, 0}, pb = {1, 1}, pc = {2, 0};
    expect("orient-cw", orient(pa, pb, pc) < 0);
    Point pd = {0, 2};
    expect("orient-ccw", orient(pa, pb, pd) > 0);

    // segments crossing
    Point s1a = {0, 0}, s1b = {4, 4}, s2a = {0, 4}, s2b = {4, 0};
    expect("seg-cross", segments_intersect(s1a, s1b, s2a, s2b));
    Point s3a = {1, 5}, s3b = {3, 5};
    expect("seg-nocross", !segments_intersect(s1a, s1b, s3a, s3b));
    // collinear touching
    Point t1 = {0, 0}, t2 = {4, 0}, t3 = {2, 0}, t4 = {6, 0};
    expect("seg-collinear-touch", segments_intersect(t1, t2, t3, t4));

    Point li;
    expect("line-intersect", line_intersection(s1a, s1b, s2a, s2b, &li));
    expect("line-int-pt", li.x == 2 && li.y == 2);
    Point par1 = {0, 0}, par2 = {1, 0}, par3 = {0, 1}, par4 = {1, 1};
    expect("line-parallel", !line_intersection(par1, par2, par3, par4, &li));

    // triangle: 2x area of (0,0)(4,0)(0,3) = 12
    Point ta = {0, 0}, tb = {4, 0}, tc = {0, 3};
    expect("tri-area2", triangle_area2(ta, tb, tc) == 12);
    expect("tri-in", point_in_triangle({1, 1}, ta, tb, tc));
    expect("tri-out", !point_in_triangle({3, 3}, ta, tb, tc));

    // polygon: square double-area 128, centroid (4,4)
    Point sq[4] = {{0, 0}, {8, 0}, {8, 8}, {0, 8}};
    expect("poly-area2", polygon_area2(sq, 4) == 128);
    Point cg = polygon_centroid(sq, 4);
    expect("poly-centroid", cg.x == 4 && cg.y == 4);
    expect("poly-in", point_in_polygon({4, 4}, sq, 4));
    expect("poly-out", !point_in_polygon({9, 9}, sq, 4));
    // concave polygon: L-shape, arm inside, notch (upper-left) outside
    Point L[6] = {{0, 0}, {6, 0}, {6, 6}, {2, 6}, {2, 2}, {0, 2}};   // L-shape (ccw-ish)
    expect("L-in", point_in_polygon({1, 1}, L, 6));
    expect("L-notch", !point_in_polygon({1, 4}, L, 6));

    // convex hull of 5 points: one interior point excluded
    Point pts[5] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {5, 5}};
    Point hull[8];
    int hn = convex_hull(pts, 5, hull);
    expect("hull-size", hn == 4);
    bool hasInner = false;
    for (int i = 0; i < hn; i++)
        if (hull[i].x == 5 && hull[i].y == 5) hasInner = true;
    expect("hull-excludes", !hasInner);
    // degenerate input
    Point d2[2] = {{0, 0}, {1, 1}};
    expect("hull-degenerate", convex_hull(d2, 2, hull) == 0);

    return g_geo_fails;
}

} // namespace gfxlib
} // namespace nefu
