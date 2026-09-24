#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 数学扩展库 —— 计算几何模块实现
#include "geometry.h"
#include "complex.h"   // d_close
#include <cmath>
#include <cstddef>

namespace nefu {
namespace mathext {

double gdist(const GPoint& a, const GPoint& b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double gcross(const GPoint& o, const GPoint& a, const GPoint& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// ---------------- 多边形面积（鞋带公式） ----------------
double polygon_area(const GPoint* poly, int n) {
    if (n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        s += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
    }
    return 0.5 * std::fabs(s);
}

// ---------------- 点在多边形内（射线法） ----------------
bool point_in_polygon(const GPoint& p, const GPoint* poly, int n) {
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        // 水平射线 y = p.y 与边 (poly[j], poly[i]) 是否相交
        if (((poly[i].y > p.y) != (poly[j].y > p.y))) {
            double xint = (poly[j].x - poly[i].x) * (p.y - poly[i].y) /
                          (poly[j].y - poly[i].y) + poly[i].x;
            if (p.x < xint) inside = !inside;
        }
    }
    return inside;
}

// ---------------- 线段相交（跨立试验 + 包围盒） ----------------
bool segments_intersect(const GPoint& a, const GPoint& b,
                        const GPoint& c, const GPoint& d) {
    double d1 = gcross(c, d, a);
    double d2 = gcross(c, d, b);
    double d3 = gcross(a, b, c);
    double d4 = gcross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0)))
        return true;
    return false;
}

// ---------------- Graham scan 凸包 ----------------
int convex_hull_graham(const GPoint* pts, int n, GPoint* out, int max_out) {
    if (n <= 0) return 0;
    // 找 y 最小（并列取 x 最小）的点作为基点
    int bi = 0;
    for (int i = 1; i < n; i++)
        if (pts[i].y < pts[bi].y ||
            (pts[i].y == pts[bi].y && pts[i].x < pts[bi].x))
            bi = i;
    GPoint base = pts[bi];
    // 拷贝并按极角排序（冒泡插入，规模小）
    GPoint* s = new GPoint[(size_t)n];
    int sn = 0;
    for (int i = 0; i < n; i++) {
        if (pts[i] == base) continue;
        s[sn++] = pts[i];
    }
    // 插入排序：极角小的在前（用叉积比较）
    for (int i = 1; i < sn; i++) {
        GPoint key = s[i];
        int j = i - 1;
        while (j >= 0 && gcross(base, s[j], key) < 0) {
            s[j + 1] = s[j];
            j--;
        }
        s[j + 1] = key;
    }
    // 栈
    GPoint* hull = new GPoint[(size_t)(n + 1)];
    int hn = 0;
    hull[hn++] = base;
    for (int i = 0; i < sn; i++) {
        while (hn >= 2 && gcross(hull[hn - 2], hull[hn - 1], s[i]) <= 0) hn--;
        hull[hn++] = s[i];
    }
    int cnt = hn < max_out ? hn : max_out;
    for (int i = 0; i < cnt; i++) out[i] = hull[i];
    delete[] s;
    delete[] hull;
    return cnt;
}

// ---------------- Jarvis march 凸包 ----------------
int convex_hull_jarvis(const GPoint* pts, int n, GPoint* out, int max_out) {
    if (n <= 0) return 0;
    // 最左点起步
    int start = 0;
    for (int i = 1; i < n; i++)
        if (pts[i].x < pts[start].x) start = i;
    int p = start, cnt = 0;
    do {
        if (cnt >= max_out) break;
        out[cnt++] = pts[p];
        int q = (p + 1) % n;
        for (int i = 0; i < n; i++)
            if (gcross(pts[p], pts[i], pts[q]) > 0) q = i;
        p = q;
    } while (p != start && cnt < max_out);
    return cnt;
}

// ---------------- 最近点对（O(n^2)） ----------------
double closest_pair(const GPoint* pts, int n, GPoint& p1, GPoint& p2) {
    double best = -1.0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            double d = gdist(pts[i], pts[j]);
            if (best < 0 || d < best) { best = d; p1 = pts[i]; p2 = pts[j]; }
        }
    if (best < 0) best = 0;
    return best;
}

// ---------------- 极角排序 ----------------
void polar_sort(const GPoint* pts, int n, const GPoint& base, GPoint* out) {
    for (int i = 0; i < n; i++) out[i] = pts[i];
    // 插入排序，叉积判序
    for (int i = 1; i < n; i++) {
        GPoint key = out[i];
        int j = i - 1;
        while (j >= 0 && gcross(base, out[j], key) < 0) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
}

// ---------------- 三角形外接圆 ----------------
GPoint circumcircle(const GPoint& a, const GPoint& b, const GPoint& c, double& out_r) {
    // 两条边的垂直平分线交点
    double D = 2.0 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
    GPoint center(0, 0);
    if (std::fabs(D) < 1e-300) { out_r = 0; return center; }
    double a2 = a.x * a.x + a.y * a.y;
    double b2 = b.x * b.x + b.y * b.y;
    double c2 = c.x * c.x + c.y * c.y;
    center.x = (a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / D;
    center.y = (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / D;
    out_r = gdist(center, a);
    return center;
}

// ---------------- 三角形内切圆 ----------------
GPoint incircle(const GPoint& a, const GPoint& b, const GPoint& c, double& out_r) {
    double A = gdist(b, c), B = gdist(a, c), C = gdist(a, b);
    double perim = A + B + C;
    GPoint center((A * a.x + B * b.x + C * c.x) / perim,
                  (A * a.y + B * b.y + C * c.y) / perim);
    double area = 0.5 * std::fabs(gcross(a, b, c));
    out_r = (perim > 0) ? 2.0 * area / perim : 0.0;
    return center;
}

// ---------------- 点到线段距离 ----------------
double point_segment_dist(const GPoint& p, const GPoint& a, const GPoint& b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-300) return gdist(p, a);
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    GPoint proj(a.x + t * dx, a.y + t * dy);
    return gdist(p, proj);
}

// ---------------- 绕点旋转 ----------------
GPoint rotate_point(const GPoint& p, const GPoint& c, double theta) {
    double dx = p.x - c.x, dy = p.y - c.y;
    double cs = std::cos(theta), sn = std::sin(theta);
    return GPoint(c.x + dx * cs - dy * sn, c.y + dx * sn + dy * cs);
}

// ---------------- 多边形质心 ----------------
GPoint polygon_centroid(const GPoint* poly, int n) {
    double a = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        double cross = poly[i].x * poly[j].y - poly[j].x * poly[i].y;
        a += cross;
        cx += (poly[i].x + poly[j].x) * cross;
        cy += (poly[i].y + poly[j].y) * cross;
    }
    if (std::fabs(a) < 1e-300) return GPoint(0, 0);
    a *= 0.5;
    return GPoint(cx / (6.0 * a), cy / (6.0 * a));
}

// ---------------- 两直线交点 ----------------
bool line_intersection(const GPoint& a, const GPoint& b,
                       const GPoint& c, const GPoint& d, GPoint& out) {
    double d1 = (b.x - a.x) * (d.y - c.y) - (b.y - a.y) * (d.x - c.x);
    if (std::fabs(d1) < 1e-300) return false;
    double t = ((c.x - a.x) * (d.y - c.y) - (c.y - a.y) * (d.x - c.x)) / d1;
    out = GPoint(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
    return true;
}
// ---------------- 凸多边形内判定 ----------------
bool point_in_convex(const GPoint& p, const GPoint* poly, int n) {
    bool has_neg = false, has_pos = false;
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        double cr = (poly[j].x - poly[i].x) * (p.y - poly[i].y) -
                    (poly[j].y - poly[i].y) * (p.x - poly[i].x);
        if (cr > 1e-12) has_pos = true;
        else if (cr < -1e-12) has_neg = true;
        if (has_pos && has_neg) return false;
    }
    return true;
}

bool rect_intersect(double ax1, double ay1, double ax2, double ay2,
                    double bx1, double by1, double bx2, double by2) {
    return !(ax2 < bx1 || bx2 < ax1 || ay2 < by1 || by2 < ay1);
}
// ---------------- 自检 ----------------
int geometry_self_test() {
    int fails = 0;
    const double EPS = 1e-6;

    // 1. 凸包：正方形 + 中心点 -> 4 个点
    GPoint sq[5] = {GPoint(0, 0), GPoint(1, 0), GPoint(1, 1), GPoint(0, 1), GPoint(0.5, 0.5)};
    GPoint hull[8];
    int hn = convex_hull_graham(sq, 5, hull, 8);
    if (hn != 4) fails++;

    // Jarvis 也应得到 4
    int hn2 = convex_hull_jarvis(sq, 5, hull, 8);
    if (hn2 != 4) fails++;

    // 2. 正方形面积 = 1
    if (!d_close(polygon_area(sq, 4), 1.0, EPS)) fails++;

    // 3. 点在多边形内：中心在正方形内，(2,2) 在外
    if (!point_in_polygon(GPoint(0.5, 0.5), sq, 4)) fails++;
    if (point_in_polygon(GPoint(2, 2), sq, 4)) fails++;

    // 4. 线段相交：(0,0)-(2,2) 与 (0,2)-(2,0) 相交；平行不相交
    if (!segments_intersect(GPoint(0, 0), GPoint(2, 2), GPoint(0, 2), GPoint(2, 0))) fails++;
    if (segments_intersect(GPoint(0, 0), GPoint(1, 0), GPoint(0, 1), GPoint(1, 1))) fails++;

    // 5. 最近点对：(0,0),(3,4),(1,1) -> 距离最近的是 (0,0)-(1,1)=sqrt2
    GPoint pts[3] = {GPoint(0, 0), GPoint(3, 4), GPoint(1, 1)};
    GPoint p1, p2;
    double d = closest_pair(pts, 3, p1, p2);
    if (!d_close(d, std::sqrt(2.0), EPS)) fails++;

    // 6. 外接圆：直角三角形 (0,0),(2,0),(0,2) 的外接圆心 (1,1) r=sqrt2
    double r;
    GPoint cc = circumcircle(GPoint(0, 0), GPoint(2, 0), GPoint(0, 2), r);
    if (!d_close(cc.x, 1.0, EPS) || !d_close(cc.y, 1.0, EPS)) fails++;

    return fails;
}

} // namespace mathext
} // namespace nefu
