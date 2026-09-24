// nefuOS 数学扩展库 —— 计算几何模块
// 点/线段/多边形基础运算：鞋带面积、Graham/Jarvis 凸包、线段相交、
// 射线法点在多边形内、最近点对、极角排序、三角形外接/内切圆。
#pragma once

namespace nefu {
namespace mathext {

// ---------------- 点 ----------------
struct GPoint {
    double x, y;
    GPoint() : x(0), y(0) {}
    GPoint(double a, double b) : x(a), y(b) {}
    bool operator==(const GPoint& o) const { return x == o.x && y == o.y; }
};

double gdist(const GPoint& a, const GPoint& b);   // 欧氏距离
double gcross(const GPoint& o, const GPoint& a, const GPoint& b);
// (a-o) x (b-o)：>0 表示 b 在 o->a 的逆时针侧

// ---------------- 多边形 ----------------
double polygon_area(const GPoint* poly, int n);    // 鞋带公式（有向面积绝对值）
bool   point_in_polygon(const GPoint& p, const GPoint* poly, int n);  // 射线法

// ---------------- 线段相交 ----------------
bool segments_intersect(const GPoint& a, const GPoint& b,
                        const GPoint& c, const GPoint& d);

// ---------------- 凸包 ----------------
// Graham scan：返回 hull 点数，结果写入 out（最多 max_out），逆时针。
int convex_hull_graham(const GPoint* pts, int n, GPoint* out, int max_out);
// Jarvis march（礼品包裹）：同上。
int convex_hull_jarvis(const GPoint* pts, int n, GPoint* out, int max_out);

// ---------------- 最近点对 ----------------
// 返回最短距离，最接近点对写入 p1/p2。O(n^2) 直接扫描（教学实现）。
double closest_pair(const GPoint* pts, int n, GPoint& p1, GPoint& p2);

// ---------------- 极角排序 ----------------
// 以 base 为原点，把 pts[0..n-1] 按极角（逆时针）排序到 out。
void polar_sort(const GPoint& pts, int n, const GPoint& base, GPoint* out);

// ---------------- 三角形外接圆 / 内切圆 ----------------
// 返回圆心，半径写出参。
GPoint circumcircle(const GPoint& a, const GPoint& b, const GPoint& c, double& out_r);
GPoint incircle(const GPoint& a, const GPoint& b, const GPoint& c, double& out_r);

// ---------------- 更多几何 ----------------
double point_segment_dist(const GPoint& p, const GPoint& a, const GPoint& b);
GPoint rotate_point(const GPoint& p, const GPoint& c, double theta);  // 绕 c 旋转
GPoint polygon_centroid(const GPoint* poly, int n);                    // 多边形质心
bool line_intersection(const GPoint& a, const GPoint& b,
                       const GPoint& c, const GPoint& d, GPoint& out); // 两直线交点
// 点在凸多边形内（顶点逆时针）：叉积同号判定。
bool point_in_convex(const GPoint& p, const GPoint* poly, int n);
// 两轴对齐矩形是否相交。
bool rect_intersect(double ax1, double ay1, double ax2, double ay2,
                    double bx1, double by1, double bx2, double by2);
// ---------------- 自检 ----------------
// 已知值：
//   凸包 of [(0,0),(1,0),(1,1),(0,1),(0.5,0.5)] = 4 个点
//   正方形面积 = 1
// 返回失败条数。
int geometry_self_test();

} // namespace mathext
} // namespace nefu
