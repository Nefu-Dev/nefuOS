// ge_math.h —— 2D 游戏引擎数学库（Q16.16 定点）
//
// 设计约束：bare 模式无 FPU，全部使用 nefu::fx Q16.16 定点运算。
// 本头文件提供：
//   - Vec2 / Vec3      定点向量
//   - Mat3             2D 仿射变换矩阵（平移/旋转/缩放，行主序）
//   - Transform2D      节点变换组件（位置/旋转/缩放）
//   - Quat             四元数（定点，用于 3D 旋转参考与插值）
//   - 插值与缓动        lerp / smoothstep / catmull / 一整套 easing 函数
//
// 所有函数均为纯逻辑（不依赖 gfxlib），可在宿主机自测中独立链接。
#pragma once

#include <stdint.h>
#include "../lib/softmath.h"

namespace nefu {
namespace gameengine {

// 定点别名：整个引擎统一用 fx::fix 作为标量类型
typedef fx::fix  fix;
typedef fx::fix64 fix64;

// 常用常量（Q16.16）
static const fix GE_ZERO = 0;
static const fix GE_ONE  = fx::FX_ONE;
static const fix GE_HALF = fx::FX_HALF;
static const fix GE_PI   = fx::FX_PI;

// 定点取最值 / 夹取 / 符号（内联，避免浮点）
inline fix ge_clamp(fix v, fix lo, fix hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
inline fix ge_max(fix a, fix b) { return a > b ? a : b; }
inline fix ge_min(fix a, fix b) { return a < b ? a : b; }
inline fix ge_sign(fix v)       { return v < 0 ? -fx::FX_ONE : (v > 0 ? fx::FX_ONE : 0); }
inline fix ge_abs(fix v)        { return fx::fx_abs(v); }

// ============================================================================
//  Vec2 —— 2D 定点向量
// ============================================================================
struct Vec2 {
    fix x;
    fix y;

    Vec2() : x(0), y(0) {}
    Vec2(fix x_, fix y_) : x(x_), y(y_) {}

    // 分量级运算（全部定点）
    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator-() const               { return Vec2(-x, -y); }
    Vec2 operator*(fix s) const         { return Vec2(fx::fx_mul(x, s), fx::fx_mul(y, s)); }
    Vec2 operator/(fix s) const         { return Vec2(fx::fx_div(x, s), fx::fx_div(y, s)); }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(fix s) { x = fx::fx_mul(x, s); y = fx::fx_mul(y, s); return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return x != o.x || y != o.y; }

    // 点积 / 叉积（2D 叉积为标量 z 分量）
    fix dot(const Vec2& o) const  { return fx::fx_mul(x, o.x) + fx::fx_mul(y, o.y); }
    fix cross(const Vec2& o) const { return fx::fx_mul(x, o.y) - fx::fx_mul(y, o.x); }

    // 长度平方 / 长度（定点开方）
    fix lensq() const { return fx::fx_mul(x, x) + fx::fx_mul(y, y); }
    fix len() const   { return fx::fx_sqrt(lensq()); }

    // 归一化：返回单位向量；零向量返回零向量（避免除零）
    Vec2 normalized() const {
        fix l = len();
        if (l == 0) return Vec2(0, 0);
        return Vec2(fx::fx_div(x, l), fx::fx_div(y, l));
    }

    // 距离
    fix dist(const Vec2& o) const { return (*this - o).len(); }
    fix distsq(const Vec2& o) const { Vec2 d = *this - o; return d.lensq(); }

    // 垂直向量（逆时针 90 度）
    Vec2 perp() const { return Vec2(-y, x); }

    // 旋转（弧度，定点三角）
    Vec2 rotated(fix rad) const {
        fix c = fx::fx_cos(rad);
        fix s = fx::fx_sin(rad);
        return Vec2(fx::fx_mul(x, c) - fx::fx_mul(y, s),
                    fx::fx_mul(x, s) + fx::fx_mul(y, c));
    }

    // 整数坐标（用于像素绘制）
    int to_ix() const { return fx::fixtoi(x); }
    int to_iy() const { return fx::fixtoi(y); }
};

// 向量数乘左结合
inline Vec2 operator*(fix s, const Vec2& v) { return v * s; }

// ============================================================================
//  Vec3 —— 3D 定点向量（供物理/3D 参考与演示）
// ============================================================================
struct Vec3 {
    fix x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(fix x_, fix y_, fix z_) : x(x_), y(y_), z(z_) {}
    Vec2 xy() const { return Vec2(x, y); }

    Vec3 operator+(const Vec3& o) const { return Vec3(x+o.x, y+o.y, z+o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x-o.x, y-o.y, z-o.z); }
    Vec3 operator*(fix s) const { return Vec3(fx::fx_mul(x,s), fx::fx_mul(y,s), fx::fx_mul(z,s)); }

    fix dot(const Vec3& o) const {
        return fx::fx_mul(x,o.x) + fx::fx_mul(y,o.y) + fx::fx_mul(z,o.z);
    }
    Vec3 cross(const Vec3& o) const {
        return Vec3(fx::fx_mul(y,o.z) - fx::fx_mul(z,o.y),
                    fx::fx_mul(z,o.x) - fx::fx_mul(x,o.z),
                    fx::fx_mul(x,o.y) - fx::fx_mul(y,o.x));
    }
    fix lensq() const { return fx::fx_mul(x,x)+fx::fx_mul(y,y)+fx::fx_mul(z,z); }
    fix len() const   { return fx::fx_sqrt(lensq()); }
    Vec3 normalized() const {
        fix l = len();
        if (l == 0) return Vec3(0,0,0);
        return Vec3(fx::fx_div(x,l), fx::fx_div(y,l), fx::fx_div(z,l));
    }
};

// ============================================================================
//  Mat3 —— 2D 仿射矩阵（行主序，3x3）
//
//   | m00 m01 m02 |     | sx*cos  -sy*sin   tx |
//   | m10 m11 m12 |  =  | sx*sin   sy*cos   ty |
//   | m20 m21 m22 |     |   0        0       1 |
//
//  变换点：p' = M * [x, y, 1]^T
// ============================================================================
struct Mat3 {
    fix m[3][3];

    Mat3() { set_identity(); }

    void set_identity() {
        m[0][0]=fx::FX_ONE; m[0][1]=0;            m[0][2]=0;
        m[1][0]=0;          m[1][1]=fx::FX_ONE;   m[1][2]=0;
        m[2][0]=0;          m[2][1]=0;            m[2][2]=fx::FX_ONE;
    }

    // 平移矩阵
    static Mat3 make_translate(fix tx, fix ty) {
        Mat3 r;
        r.m[0][2] = tx;
        r.m[1][2] = ty;
        return r;
    }
    // 缩放矩阵
    static Mat3 make_scale(fix sx, fix sy) {
        Mat3 r;
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        return r;
    }
    // 旋转矩阵（弧度）
    static Mat3 make_rotate(fix rad) {
        Mat3 r;
        fix c = fx::fx_cos(rad);
        fix s = fx::fx_sin(rad);
        r.m[0][0] = c;  r.m[0][1] = -s;
        r.m[1][0] = s;  r.m[1][1] = c;
        return r;
    }

    // 矩阵乘法 this = a * b（列向量约定：先应用 b，再应用 a）
    static Mat3 mul(const Mat3& a, const Mat3& b) {
        Mat3 r;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                fix acc = 0;
                for (int k = 0; k < 3; k++) {
                    acc += fx::fx_mul(a.m[i][k], b.m[k][j]);
                }
                r.m[i][j] = acc;
            }
        }
        return r;
    }

    // 变换一个 2D 点（忽略齐次行）
    Vec2 transform_point(const Vec2& p) const {
        return Vec2(
            fx::fx_mul(m[0][0], p.x) + fx::fx_mul(m[0][1], p.y) + m[0][2],
            fx::fx_mul(m[1][0], p.x) + fx::fx_mul(m[1][1], p.y) + m[1][2]);
    }
    // 变换一个方向向量（不加平移列）
    Vec2 transform_vec(const Vec2& v) const {
        return Vec2(
            fx::fx_mul(m[0][0], v.x) + fx::fx_mul(m[0][1], v.y),
            fx::fx_mul(m[1][0], v.x) + fx::fx_mul(m[1][1], v.y));
    }

    // 求逆（针对仿射矩阵：仅逆线性部分 + 重算平移）
    Mat3 inverse() const {
        fix det = fx::fx_mul(m[0][0], m[1][1]) - fx::fx_mul(m[0][1], m[1][0]);
        Mat3 r;
        if (det == 0) { r.set_identity(); return r; }
        fix inv = fx::fx_div(fx::FX_ONE, det);
        r.m[0][0] = fx::fx_mul( m[1][1], inv);
        r.m[0][1] = fx::fx_mul(-m[0][1], inv);
        r.m[1][0] = fx::fx_mul(-m[1][0], inv);
        r.m[1][1] = fx::fx_mul( m[0][0], inv);
        // 逆平移：t' = -R^-1 * t
        fix tx = m[0][2], ty = m[1][2];
        r.m[0][2] = -(fx::fx_mul(r.m[0][0], tx) + fx::fx_mul(r.m[0][1], ty));
        r.m[1][2] = -(fx::fx_mul(r.m[1][0], tx) + fx::fx_mul(r.m[1][1], ty));
        r.m[2][2] = fx::FX_ONE;
        return r;
    }
};

// ============================================================================
//  Transform2D —— 节点变换组件
// ============================================================================
struct Transform2D {
    Vec2 position;
    fix  rotation;     // 弧度
    Vec2 scale;

    Transform2D() : position(0, 0), rotation(0), scale(fx::FX_ONE, fx::FX_ONE) {}

    // 从局部到世界矩阵（T * R * S）
    Mat3 to_matrix() const {
        Mat3 t = Mat3::make_translate(position.x, position.y);
        Mat3 r = Mat3::make_rotate(rotation);
        Mat3 s = Mat3::make_scale(scale.x, scale.y);
        return Mat3::mul(Mat3::mul(t, r), s);
    }

    // 世界矩阵：父矩阵 * 本节点局部矩阵
    Mat3 to_world(const Mat3& parent) const {
        return Mat3::mul(parent, to_matrix());
    }

    // 局部点 -> 世界点
    Vec2 local_to_world(const Vec2& p, const Mat3& parent) const {
        return to_world(parent).transform_point(p);
    }
};

// ============================================================================
//  Quat —— 定点四元数 (x, y, z, w)
//  仅用于演示四元数乘法 / 插值 / 转旋转矩阵；引擎主体是 2D 的。
// ============================================================================
struct Quat {
    fix x, y, z, w;
    Quat() : x(0), y(0), z(0), w(fx::FX_ONE) {}
    Quat(fix x_, fix y_, fix z_, fix w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return Quat(0, 0, 0, fx::FX_ONE); }

    Quat operator*(const Quat& o) const {
        return Quat(
            fx::fx_mul(w,o.x)+fx::fx_mul(x,o.w)+fx::fx_mul(y,o.z)-fx::fx_mul(z,o.y),
            fx::fx_mul(w,o.y)-fx::fx_mul(x,o.z)+fx::fx_mul(y,o.w)+fx::fx_mul(z,o.x),
            fx::fx_mul(w,o.z)+fx::fx_mul(x,o.y)-fx::fx_mul(y,o.x)+fx::fx_mul(z,o.w),
            fx::fx_mul(w,o.w)-fx::fx_mul(x,o.x)-fx::fx_mul(y,o.y)-fx::fx_mul(z,o.z));
    }
    fix lensq() const {
        return fx::fx_mul(x,x)+fx::fx_mul(y,y)+fx::fx_mul(z,z)+fx::fx_mul(w,w);
    }
    Quat normalized() const {
        fix l = fx::fx_sqrt(lensq());
        if (l == 0) return identity();
        return Quat(fx::fx_div(x,l), fx::fx_div(y,l), fx::fx_div(z,l), fx::fx_div(w,l));
    }
    // 球面线性插值（近似：归一化线性插值，定点足够）
    static Quat slerp(const Quat& a, const Quat& b, fix t) {
        fix d = fx::fx_mul(a.x,b.x)+fx::fx_mul(a.y,b.y)+fx::fx_mul(a.z,b.z)+fx::fx_mul(a.w,b.w);
        fix s = d < 0 ? -fx::FX_ONE : fx::FX_ONE;
        Quat r(
            fx::fx_mul(a.x, fx::FX_ONE - t) + fx::fx_mul(fx::fx_mul(b.x, s), t),
            fx::fx_mul(a.y, fx::FX_ONE - t) + fx::fx_mul(fx::fx_mul(b.y, s), t),
            fx::fx_mul(a.z, fx::FX_ONE - t) + fx::fx_mul(fx::fx_mul(b.z, s), t),
            fx::fx_mul(a.w, fx::FX_ONE - t) + fx::fx_mul(fx::fx_mul(b.w, s), t));
        return r.normalized();
    }
};

// ============================================================================
//  插值函数
// ============================================================================
// 线性插值（t 为 Q16.16，范围 [0,1]）
inline fix lerp(fix a, fix b, fix t) {
    return a + fx::fx_mul(b - a, t);
}
// Vec2 线性插值
inline Vec2 lerp(const Vec2& a, const Vec2& b, fix t) {
    return Vec2(lerp(a.x, b.x, t), lerp(a.y, b.y, t));
}
// smoothstep：t 在 [0,1] 的平滑阶跃，返回 t*t*(3-2t)
inline fix smoothstep(fix t) {
    t = ge_clamp(t, 0, fx::FX_ONE);
    fix t2 = fx::fx_mul(t, t);
    fix three_minus_2t = fx::itofix(3) - fx::fx_mul(2, t);   // 3 - 2t
    return fx::fx_mul(t2, three_minus_2t);
}
// 贝塞尔一维（三次）
inline fix bezier3(fix p0, fix p1, fix p2, fix p3, fix t) {
    fix mt = fx::FX_ONE - t;
    fix a = fx::fx_mul(fx::fx_mul(mt, mt), mt);
    fix b = fx::fx_mul(fx::fx_mul(3, t), fx::fx_mul(mt, mt));
    fix c = fx::fx_mul(fx::fx_mul(3, fx::fx_mul(t, t)), mt);
    fix d = fx::fx_mul(fx::fx_mul(t, t), t);
    return fx::fx_mul(p0,a)+fx::fx_mul(p1,b)+fx::fx_mul(p2,c)+fx::fx_mul(p3,d);
}

// ============================================================================
//  缓动函数（Easing）—— 输入 t ∈ [0,1]（Q16.16），输出进度 ∈ [0,1]
//  命名约定：easeIn* 由慢到快；easeOut* 由快到慢；easeInOut* 两端都慢。
// ============================================================================
enum EaseType {
    EASE_LINEAR = 0,
    EASE_IN_QUAD, EASE_OUT_QUAD, EASE_INOUT_QUAD,
    EASE_IN_CUBIC, EASE_OUT_CUBIC, EASE_INOUT_CUBIC,
    EASE_IN_SINE,  EASE_OUT_SINE,  EASE_INOUT_SINE,
    EASE_IN_EXPO,  EASE_OUT_EXPO,  EASE_INOUT_EXPO,
    EASE_IN_BACK,  EASE_OUT_BACK,  EASE_INOUT_BACK,
    EASE_OUT_BOUNCE, EASE_OUT_ELASTIC,
    EASE_COUNT
};

// 统一入口：按类型计算缓动系数
fix easing(EaseType type, fix t);

// 取某类型的显示名（用于 UI 演示）
const char* easing_name(EaseType type);

// ============================================================================
//  Color —— RGBA8888 颜色工具（打包成 uint32，与 gfx 一致）
// ============================================================================
struct Color {
    uint8_t r, g, b, a;
    Color() : r(0), g(0), b(0), a(255) {}
    Color(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255)
        : r(R), g(G), b(B), a(A) {}

    uint32_t pack() const {
        return ((uint32_t)a << 24) | ((uint32_t)b << 16) |
               ((uint32_t)g << 8) | (uint32_t)r;
    }
    static Color unpack(uint32_t c) {
        return Color((uint8_t)(c & 0xFF),
                     (uint8_t)((c >> 8) & 0xFF),
                     (uint8_t)((c >> 16) & 0xFF),
                     (uint8_t)((c >> 24) & 0xFF));
    }
    static Color lerp(const Color& x, const Color& y, fix t) {
        int tt = fx::fixtoi(t << 8);
        if (tt < 0) tt = 0; if (tt > 256) tt = 256;
        Color o;
        o.r = (uint8_t)((x.r * (256 - tt) + y.r * tt) >> 8);
        o.g = (uint8_t)((x.g * (256 - tt) + y.g * tt) >> 8);
        o.b = (uint8_t)((x.b * (256 - tt) + y.b * tt) >> 8);
        o.a = (uint8_t)((x.a * (256 - tt) + y.a * tt) >> 8);
        return o;
    }
    Color scale(int k) const {
        Color o;
        o.r = (uint8_t)((int)r * k / 255);
        o.g = (uint8_t)((int)g * k / 255);
        o.b = (uint8_t)((int)b * k / 255);
        o.a = a;
        return o;
    }
};

inline Mat3 mat3_inverse(const Mat3& m) {
    fix a = m.m[0][0], b = m.m[0][1], tx = m.m[0][2];
    fix c = m.m[1][0], d = m.m[1][1], ty = m.m[1][2];
    fix det = fx::fx_mul(a, d) - fx::fx_mul(b, c);
    Mat3 r;
    if (det == 0) { r = Mat3(); return r; }
    r.m[0][0] = fx::fx_div(d, det);
    r.m[0][1] = fx::fx_div(-b, det);
    r.m[1][0] = fx::fx_div(-c, det);
    r.m[1][1] = fx::fx_div(a, det);
    fix ntx = -tx, nty = -ty;
    r.m[0][2] = fx::fx_mul(r.m[0][0], ntx) + fx::fx_mul(r.m[0][1], nty);
    r.m[1][2] = fx::fx_mul(r.m[1][0], ntx) + fx::fx_mul(r.m[1][1], nty);
    r.m[2][0] = 0; r.m[2][1] = 0; r.m[2][2] = fx::FX_ONE;
    return r;
}

const int GE_STACK_MAX = 16;
struct TransformStack {
    Mat3 stack[GE_STACK_MAX];
    int  top;
    TransformStack() : top(0) { stack[0] = Mat3(); }
    void push() {
        if (top < GE_STACK_MAX - 1) { top++; stack[top] = stack[top - 1]; }
    }
    void pop() { if (top > 0) top--; }
    Mat3& current() { return stack[top]; }
    void mul(const Mat3& m) { stack[top] = Mat3::mul(stack[top], m); }
    void reset() { top = 0; stack[0] = Mat3(); }
};
// ============================================================================
//  自测
// ============================================================================
// 返回失败数（0 = 全部通过）

// ============================================================================
//  Vec2 反射/投影/线性代数工具
// ============================================================================
// 投影 a 到 b 上
inline Vec2 vec_project(const Vec2& a, const Vec2& b) {
    fix s = fx::fx_div(a.dot(b), b.dot(b));
    return Vec2(fx::fx_mul(b.x, s), fx::fx_mul(b.y, s));
}
// 反射：v 关于法线 n（单位向量）
inline Vec2 vec_reflect(const Vec2& v, const Vec2& n) {
    fix d2 = fx::fx_mul(v.dot(n), fx::itofix(2));
    return Vec2(v.x - fx::fx_mul(d2, n.x), v.y - fx::fx_mul(d2, n.y));
}
// 距离
inline fix vec_distance(const Vec2& a, const Vec2& b) {
    fix dx = a.x - b.x, dy = a.y - b.y;
    return fx::fx_sqrt(fx::fx_mul(dx, dx) + fx::fx_mul(dy, dy));
}
// 线性插值两个颜色（打包 uint32）
inline uint32_t lerp_color(uint32_t c1, uint32_t c2, fix t) {
    Color a = Color::unpack(c1), b = Color::unpack(c2);
    return Color::lerp(a, b, t).pack();
}


// ============================================================================
//  MathUtils —— 通用数学工具
// ============================================================================
inline fix fx_clamp(fix v, fix lo, fix hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
inline int int_clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
inline fix fx_lerp(fix a, fix b, fix t) {
    return a + fx::fx_mul(b - a, t);
}
// 平滑趋近：current 向 target 以 rate 趋近（帧率无关）
inline fix fx_smooth(fix current, fix target, fix rate, fix dt_s) {
    fix diff = target - current;
    return current + fx::fx_mul(fx::fx_mul(diff, rate), dt_s);
}
// 角度插值（最短路径）
inline fix fx_angle_lerp(fix a, fix b, fix t) {
    fix diff = b - a;
    // 归一化到 -PI..PI
    while (diff > fx::FX_PI)  diff -= fx::FX_2PI;
    while (diff < -fx::FX_PI) diff += fx::FX_2PI;
    return a + fx::fx_mul(diff, t);
}
int ge_math_self_test();

} // namespace gameengine
} // namespace nefu
