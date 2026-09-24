// ============================================================================
// nefuOS 光线追踪引擎 —— rtmath: 定点(Q16.16) 三维数学基础
// ----------------------------------------------------------------------------
// bare 模式无 FPU，所有标量统一用 nefu::fx::fix（int32_t，Q16.16）。
// 本文件提供路径追踪所需的全部线性代数与相交测试原语：
//   - RTVec3 / RTVec4：点 / 向量 / 颜色（颜色即 RGB 三分量，值域 [0,1]）
//   - RTMat3 / RTMat4：旋转法线矩阵 / 模型视图投影矩阵
//   - RTRay：origin + t*dir
//   - RTPlane / RTAABB / RTSphere：包围与几何
//   - 相交测试：ray-sphere / ray-plane / ray-triangle(Möller–Trumbore) /
//               ray-AABB(slab) / ray-disc
//   - 反射 / 折射 / Schlick / 半球重要性采样辅助
// 设计约定：
//   1. 命名空间 nefu::raytrace，中文注释；
//   2. 不使用 STL / 异常 / RTTI / malloc；
//   3. 每个 self_test() 返回"失败条数"，0 表示全部通过；
//   4. 坐标范围建议 ±100 以内，避免 Q16.16 溢出。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../lib/softmath.h"

namespace nefu {
namespace raytrace {

// 定点标量别名：一个"世界单位" = FX_ONE = 65536
typedef fx::fix    rtfx;
typedef fx::fix64  rtfx64;

// 常用常量（Q16.16）
static const rtfx RT_PI     = fx::FX_PI;      // 3.14159
static const rtfx RT_2PI    = fx::FX_2PI;     // 6.28319
static const rtfx RT_PI_2   = fx::FX_PI_2;    // 1.57080
static const rtfx RT_ONE    = fx::FX_ONE;     // 1.0
static const rtfx RT_HALF   = fx::FX_HALF;    // 0.5
static const rtfx RT_EPS    = 2;              // ~0.00003 阴影偏移
static const rtfx RT_INF    = 0x7FFFFFFF;     // 远截断

// 定点换算小工具（内联，避免函数调用开销）
inline rtfx rt_itofx(int i)        { return fx::itofix(i); }
inline int   rt_fxtoi(rtfx f)      { return fx::fixtoi(f); }
inline rtfx  rt_mul(rtfx a, rtfx b){ return fx::fx_mul(a, b); }
inline rtfx  rt_div(rtfx a, rtfx b){ return fx::fx_div(a, b); }
inline rtfx  rt_sqrt(rtfx a)       { return fx::fx_sqrt(a); }
inline rtfx  rt_abs(rtfx a)        { return fx::fx_abs(a); }
inline rtfx  rt_min(rtfx a, rtfx b){ return a < b ? a : b; }
inline rtfx  rt_max(rtfx a, rtfx b){ return a > b ? a : b; }
// 1/sqrt(x)：定点倒数平方根
inline rtfx  rt_rsqrt(rtfx a)      { return fx::fx_div(RT_ONE, fx::fx_sqrt(a)); }
// 把 x 夹到 [lo,hi]
inline rtfx  rt_clamp(rtfx x, rtfx lo, rtfx hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}
// 近似相等（tolerance 是 Q16.16 绝对误差）
inline bool  rt_near(rtfx a, rtfx b, rtfx tol) {
    rtfx d = a - b; if (d < 0) d = -d;
    return d <= tol;
}

// ============================================================================
// RTVec3 —— 三维向量（点 / 法线 / 方向 / 颜色 RGB 共用）
// ============================================================================
struct RTVec3 {
    rtfx x, y, z;
    RTVec3() : x(0), y(0), z(0) {}
    RTVec3(rtfx x_, rtfx y_, rtfx z_) : x(x_), y(y_), z(z_) {}
    // 用整数分量构造（单位方便写测试）
    static RTVec3 from_int(int a, int b, int c) {
        return RTVec3(fx::itofix(a), fx::itofix(b), fx::itofix(c));
    }

    RTVec3 operator+(const RTVec3& o) const { return RTVec3(x + o.x, y + o.y, z + o.z); }
    RTVec3 operator-(const RTVec3& o) const { return RTVec3(x - o.x, y - o.y, z - o.z); }
    RTVec3 operator-() const                { return RTVec3(-x, -y, -z); }
    // 标量乘 / 除
    RTVec3 operator*(rtfx s) const          { return RTVec3(rt_mul(x, s), rt_mul(y, s), rt_mul(z, s)); }
    RTVec3 operator/(rtfx s) const          { return RTVec3(rt_div(x, s), rt_div(y, s), rt_div(z, s)); }
    // 分量乘（颜色调制 / Schlick 权重）
    RTVec3 operator*(const RTVec3& o) const{ return RTVec3(rt_mul(x, o.x), rt_mul(y, o.y), rt_mul(z, o.z)); }
    RTVec3& operator+=(const RTVec3& o)    { x += o.x; y += o.y; z += o.z; return *this; }
    RTVec3& operator-=(const RTVec3& o)    { x -= o.x; y -= o.y; z -= o.z; return *this; }
    RTVec3& operator*=(rtfx s)             { x = rt_mul(x, s); y = rt_mul(y, s); z = rt_mul(z, s); return *this; }

    rtfx dot(const RTVec3& o) const {
        return rt_mul(x, o.x) + rt_mul(y, o.y) + rt_mul(z, o.z);
    }
    RTVec3 cross(const RTVec3& o) const {
        return RTVec3(rt_mul(y, o.z) - rt_mul(z, o.y),
                      rt_mul(z, o.x) - rt_mul(x, o.z),
                      rt_mul(x, o.y) - rt_mul(y, o.x));
    }
    rtfx length_sq() const { return rt_mul(x, x) + rt_mul(y, y) + rt_mul(z, z); }
    rtfx length()   const { return rt_sqrt(length_sq()); }
    // 单位化（零向量安全返回 0）
    RTVec3 normalized() const {
        rtfx l2 = length_sq();
        if (l2 <= 0) return RTVec3(0, 0, 0);
        rtfx inv = rt_rsqrt(l2);
        return RTVec3(rt_mul(x, inv), rt_mul(y, inv), rt_mul(z, inv));
    }
    // 逐分量夹取（颜色用）
    RTVec3 clamp(rtfx lo, rtfx hi) const {
        return RTVec3(rt_clamp(x, lo, hi), rt_clamp(y, lo, hi), rt_clamp(z, lo, hi));
    }
};

// 向量标量乘交换律
inline RTVec3 operator*(rtfx s, const RTVec3& v) { return v * s; }

// 线性插值 t in [0,1]
inline RTVec3 rt_lerp(const RTVec3& a, const RTVec3& b, rtfx t) {
    return a + (b - a) * t;
}

// 反射：I 入射（指向表面），N 单位法线；返回指向表面外的反射方向。
// R = I - 2*(I·N)*N
RTVec3 rt_reflect_vec(const RTVec3& I, const RTVec3& N);

// 折射：I 单位入射，N 单位法线，eta = n1/n2；返回是否成功（全反射时 false）
bool   rt_refract_vec(const RTVec3& I, const RTVec3& N, rtfx eta, RTVec3& out);

// Schlick 近似：菲涅尔反射率 R0 = (1-eta)^2/(1+eta)^2
rtfx   rt_schlick(rtfx cos_theta, rtfx R0);

// 半球余弦权重采样：u,v in [0,1)（由 PRNG 给出），N 是法线
// 返回以 N 为极轴的单位采样方向（重要性采样 pdf = cos/pi）
RTVec3 rt_cosine_hemisphere_sample(const RTVec3& N, rtfx u, rtfx v);

// ============================================================================
// RTRay —— 射线：origin + t * dir（dir 单位化）
// ============================================================================
struct RTRay {
    RTVec3 origin;
    RTVec3 dir;
    rtfx   tmin;     // 近截断（阴影偏移）
    rtfx   tmax;     // 远截断
    RTRay() : tmin(RT_EPS), tmax(RT_INF) {}
    RTRay(const RTVec3& o, const RTVec3& d, rtfx mn = RT_EPS, rtfx mx = RT_INF)
        : origin(o), dir(d.normalized()), tmin(mn), tmax(mx) {}
    RTVec3 point_at(rtfx t) const { return origin + dir * t; }
};

// ============================================================================
// RTAABB —— 轴对齐包围盒
// ============================================================================
struct RTAABB {
    RTVec3 mn;   // 最小角
    RTVec3 mx;   // 最大角
    RTAABB() : mn(RTVec3(RT_INF, RT_INF, RT_INF)),
               mx(RTVec3(-RT_INF, -RT_INF, -RT_INF)) {}
    RTAABB(const RTVec3& a, const RTVec3& b) : mn(a), mx(b) {}
    void expand(const RTVec3& p) {
        if (p.x < mn.x) mn.x = p.x; if (p.x > mx.x) mx.x = p.x;
        if (p.y < mn.y) mn.y = p.y; if (p.y > mx.y) mx.y = p.y;
        if (p.z < mn.z) mn.z = p.z; if (p.z > mx.z) mx.z = p.z;
    }
    void expand(const RTAABB& b) { expand(b.mn); expand(b.mx); }
    RTVec3 center() const { return (mn + mx) * RT_HALF; }
    RTVec3 extent() const { return (mx - mn) * RT_HALF; }
    // 表面积（用于 SAH）；返回 Q16.16
    rtfx surface_area() const {
        RTVec3 e = mx - mn;
        rtfx sx = rt_mul(e.x, e.y);
        rtfx sy = rt_mul(e.y, e.z);
        rtfx sz = rt_mul(e.z, e.x);
        return (sx + sy + sz) * rt_itofx(2);
    }
    // 最长轴：0=x 1=y 2=z
    int longest_axis() const {
        RTVec3 e = mx - mn;
        if (e.x >= e.y && e.x >= e.z) return 0;
        if (e.y >= e.z) return 1;
        return 2;
    }
};

// slab 法射线-AABB：命中返回 true，并把 t 写进 tnear/tfar
bool rt_ray_aabb(const RTRay& ray, const RTAABB& box, rtfx& tnear, rtfx& tfar);

// ============================================================================
// RTVec4 —— 四维齐次坐标（投影矩阵用）
// ============================================================================
struct RTVec4 {
    rtfx x, y, z, w;
    RTVec4() : x(0), y(0), z(0), w(0) {}
    RTVec4(rtfx x_, rtfx y_, rtfx z_, rtfx w_) : x(x_), y(y_), z(z_), w(w_) {}
    RTVec3 xyz() const { return RTVec3(x, y, z); }
};

// ============================================================================
// RTPlane —— n·p + d = 0（n 单位法线）
// ============================================================================
struct RTPlane {
    RTVec3 n;
    rtfx   d;
    RTPlane() : d(0) {}
    RTPlane(const RTVec3& n_, rtfx d_) : n(n_.normalized()), d(d_) {}
    rtfx signed_distance(const RTVec3& p) const { return n.dot(p) + d; }
};

// ============================================================================
// RTMat3 —— 3x3（法线变换 / 局部->世界旋转），行主序 m[row][col]
// ============================================================================
struct RTMat3 {
    rtfx m[3][3];
    RTMat3();
    static RTMat3 identity();
    rtfx* operator[](int r) { return m[r]; }
    const rtfx* operator[](int r) const { return m[r]; }
};
RTVec3 operator*(const RTMat3& M, const RTVec3& v);
RTMat3 rt_mat3_mul(const RTMat3& A, const RTMat3& B);
RTMat3 rt_mat3_transpose(const RTMat3& M);

// ============================================================================
// RTMat4 —— 4x4（列向量约定 v'=M*v），用于相机 lookAt / 透视
// ============================================================================
struct RTMat4 {
    rtfx m[4][4];
    RTMat4();
    static RTMat4 identity();
    rtfx* operator[](int r) { return m[r]; }
    const rtfx* operator[](int r) const { return m[r]; }
};
RTMat4 rt_mat4_mul(const RTMat4& A, const RTMat4& B);
// lookAt：eye 位置，target 注视点，up 上方向（世界坐标系）
RTMat4 rt_mat4_look_at(const RTVec3& eye, const RTVec3& target, const RTVec3& up);
// 取旋转部分（左上 3x3）
RTMat3 rt_mat4_get_rot(const RTMat4& M);
// 透视投影：fovy 弧度，aspect 宽高比，near/far 近远裁剪面
RTMat4 rt_mat4_perspective(rtfx fovy, rtfx aspect, rtfx near, rtfx far);
// 正交投影
RTMat4 rt_mat4_ortho(rtfx l, rtfx r, rtfx b, rtfx t, rtfx n, rtfx f);
// 用矩阵变换点（取 w=1，透视除法）
RTVec3 rt_mat4_xform_point(const RTMat4& M, const RTVec3& p);
// 用矩阵变换方向/法线（忽略平移，transpose-inverse 的近似）
RTVec3 rt_mat4_xform_vec(const RTMat4& M, const RTVec3& v);

// ============================================================================
// 颜色工具（Q16.16 RGB）
// ============================================================================
// Reinhard tonemap: c/(1+c)
RTVec3 rt_color_reinhard(const RTVec3& c);
// gamma 校正（近似 sqrt）
RTVec3 rt_color_gamma(const RTVec3& c, rtfx g);
// 简单平均
RTVec3 rt_color_avg(const RTVec3* cols, int n);
// 转 0xRRGGBB（已在 [0,1] 区间）
uint32_t rt_color_to_rgb888(const RTVec3& c);

// ============================================================================
// 高级相交测试
// ============================================================================
// 解析法求圆环：中心 c，管半径 r_tube，环半径 r_major
// 射线-圆环是四次方程，这里用包围盒步进 + 二分精化（数值鲁棒）
rtfx rt_ray_torus(const RTRay& ray, const RTVec3& c, rtfx r_major, rtfx r_tube);
// 射线-有向包围盒（OBB）：中心 c，半extent e，旋转矩阵 R
rtfx rt_ray_obb(const RTRay& ray, const RTVec3& c, const RTVec3& e, const RTMat3& R);
// 射线-三角形带几何参数（返回 t，填 bary）
struct RTBary { rtfx u, v, w; };
bool rt_ray_tri_bary(const RTRay& ray, const RTVec3& a, const RTVec3& b,
                     const RTVec3& c, RTBary& out);

// ============================================================================
// 相交测试：返回命中距离 t，未命中返回 -1（注意 -1 在 Q16.16 下是负值）
// ============================================================================
// 球：中心 c 半径 r
rtfx rt_ray_sphere(const RTRay& ray, const RTVec3& c, rtfx r);
// 平面：法线 n 偏移 d
rtfx rt_ray_plane(const RTRay& ray, const RTPlane& p);
// Möller–Trumbore 三角形：a,b,c 三点；u,v 输出重心坐标
rtfx rt_ray_triangle(const RTRay& ray, const RTVec3& a, const RTVec3& b,
                     const RTVec3& c, rtfx& u, rtfx& v);
// 圆盘：中心 c 法线 n 半径 r
rtfx rt_ray_disc(const RTRay& ray, const RTVec3& c, const RTVec3& n, rtfx r);

// ============================================================================
// 确定性 PRNG（蒙特卡洛采样用）：xorshift32，返回 [0,1) 的 Q16.16
// ============================================================================
struct RTRng {
    uint32_t s;
    RTRng(uint32_t seed = 0x12345678u) : s(seed ? seed : 1u) {}
    uint32_t next_u32() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    // 返回 [0,1) 的 Q16.16
    rtfx next_fx() { return (rtfx)(next_u32() & 0xFFFF); }
    // 返回 [-1,1) 的 Q16.16
    rtfx next_signed() { return (rtfx)(next_u32() & 0x1FFFF) - 65536; }
};

// ============================================================================
// self test：返回失败条数（0 == 全绿）
// ============================================================================
rtfx rt_color_luminance(const RTVec3& c);
RTVec3 rt_color_contrast(const RTVec3& c, rtfx k);
RTVec3 rt_color_saturate(const RTVec3& c, rtfx s);
int rtmath_self_test();

} // namespace raytrace
} // namespace nefu
