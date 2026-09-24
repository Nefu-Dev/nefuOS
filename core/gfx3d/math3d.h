// ============================================================================
// nefuOS 3D 图形库 —— math3d: 3D 数学基础
// ----------------------------------------------------------------------------
// 本文件提供软件渲染器所需的全部线性代数工具：
//   - Vec2 / Vec3 / Vec4：二维、三维、齐次向量
//   - Mat2 / Mat3 / Mat4：二阶、三阶、四阶矩阵（行主序，列向量约定 v' = M*v）
//   - Quat：四元数（旋转表示，避免万向节死锁）
//   - 变换矩阵：平移 / 旋转 / 缩放 / 绕任意轴旋转
//   - 投影矩阵：透视投影 / 正交投影
//   - lookAt 相机矩阵
//   - 几何图元：平面、射线、AABB、球体、OBB
//   - 相交测试：ray-sphere / ray-plane / ray-triangle / ray-aabb
//
// 设计约定（教学友好）：
//   1. 所有标量用 double，host 构建有 FPU，精度足够；
//   2. 矩阵按列向量约定：世界坐标变换写成 M = T * R * S，向量 v' = M * v；
//   3. 所有 self_test() 返回"失败条数"，0 表示全部通过；
//   4. 不使用 STL、不抛异常、不用 RTTI。
// ============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace nefu {
namespace gfx3d {

// 常用数学常量（用 double 字面量，避免依赖 <cmath> 的 PI 宏）
const double PI_D   = 3.14159265358979323846;
const double TAU_D  = 6.28318530717958647692;
const double EPS_D  = 1e-9;

// 角度 <-> 弧度
inline double deg2rad(double d) { return d * PI_D / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / PI_D; }

// ============================================================================
// Vec2 —— 二维向量（纹理坐标等）
// ============================================================================
struct Vec2 {
    double x, y;
    Vec2() : x(0), y(0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2& o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator-() const { return Vec2(-x, -y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }

    double length_sq() const { return x * x + y * y; }
    double length() const;
    Vec2   normalized() const;
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
};

// 线性插值（t 在 [0,1]）
Vec2 lerp(const Vec2& a, const Vec2& b, double t);

// ============================================================================
// Vec3 —— 三维向量（位置 / 法线 / 颜色）
// ============================================================================
struct Vec3 {
    double x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
    Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }
    Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(double s) const { return Vec3(x / s, y / s, z / s); }
    // 分量乘（用于颜色调制）
    Vec3 operator*(const Vec3& o) const { return Vec3(x*o.x, y*o.y, z*o.z); }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

    double length_sq() const { return x * x + y * y + z * z; }
    double length() const;
    Vec3   normalized() const;

    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3   cross(const Vec3& o) const {
        // 叉乘：返回与 (this, o) 都垂直的向量，方向由右手定则决定
        return Vec3(y * o.z - z * o.y,
                    z * o.x - x * o.z,
                    x * o.y - y * o.x);
    }
};

Vec3 lerp(const Vec3& a, const Vec3& b, double t);
// 反射向量：I 入射方向（指向表面），N 单位法线
Vec3 reflect(const Vec3& I, const Vec3& N);
// 半程向量（Blinn-Phong）：L 指向光源，V 指向视点
Vec3 half_vector(const Vec3& L, const Vec3& V);

// ============================================================================
// Vec4 —— 齐次坐标点 / 向量
// ============================================================================
struct Vec4 {
    double x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(1) {}
    Vec4(double x_, double y_, double z_, double w_ = 1.0)
        : x(x_), y(y_), z(z_), w(w_) {}
    explicit Vec4(const Vec3& v, double w_ = 1.0)
        : x(v.x), y(v.y), z(v.z), w(w_) {}

    Vec3 xyz() const { return Vec3(x, y, z); }
    // 透视除法：把齐次坐标投影到 w=1 平面
    Vec3 perspective_divide() const {
        if (w == 0.0) return Vec3(x, y, z);
        double iw = 1.0 / w;
        return Vec3(x * iw, y * iw, z * iw);
    }

    // 齐次向量算术（用于裁剪插值）
    Vec4 operator+(const Vec4& o) const { return Vec4(x+o.x, y+o.y, z+o.z, w+o.w); }
    Vec4 operator-(const Vec4& o) const { return Vec4(x-o.x, y-o.y, z-o.z, w-o.w); }
    Vec4 operator*(double s) const { return Vec4(x*s, y*s, z*s, w*s); }
    Vec4 operator/(double s) const { return Vec4(x/s, y/s, z/s, w/s); }
};

// ============================================================================
// Mat2 —— 2x2 矩阵（行主序 m[r][c]）
// ============================================================================
struct Mat2 {
    double m[2][2];
    Mat2();                       // 单位矩阵
    static Mat2 identity();
    static Mat2 zero();
    double* operator[](int r) { return m[r]; }
    const double* operator[](int r) const { return m[r]; }
};
Mat2 operator*(const Mat2& A, const Mat2& B);
Vec2 operator*(const Mat2& M, const Vec2& v);
Mat2 transpose(const Mat2& M);
double determinant(const Mat2& M);
Mat2 inverse(const Mat2& M);

// ============================================================================
// Mat3 —— 3x3 矩阵（旋转 / 缩放 / 法线变换）
// ============================================================================
struct Mat3 {
    double m[3][3];
    Mat3();
    static Mat3 identity();
    static Mat3 zero();
    double* operator[](int r) { return m[r]; }
    const double* operator[](int r) const { return m[r]; }
};
Mat3 operator*(const Mat3& A, const Mat3& B);
Vec3 operator*(const Mat3& M, const Vec3& v);
Mat3 transpose(const Mat3& M);
double determinant(const Mat3& M);
Mat3 inverse(const Mat3& M);
// 绕轴旋转矩阵（角度为弧度，axis 不必单位化但会归一）
Mat3 mat3_rotate_axis(const Vec3& axis, double rad);

// ============================================================================
// Mat4 —— 4x4 矩阵（完整模型/视图/投影管线）
// 约定：列向量，m[行][列]，v' = M * v。
// ============================================================================
struct Mat4 {
    double m[4][4];
    Mat4();
    static Mat4 identity();
    static Mat4 zero();
    double* operator[](int r) { return m[r]; }
    const double* operator[](int r) const { return m[r]; }
};
Mat4 operator*(const Mat4& A, const Mat4& B);
Vec4 operator*(const Mat4& M, const Vec4& v);
Mat4 transpose(const Mat4& M);
double determinant(const Mat4& M);
Mat4 inverse(const Mat4& M);

// ---- 模型变换（右乘顺序：先 S，再 R，最后 T；即 M = T * R * S）----
Mat4 mat4_translate(double x, double y, double z);
Mat4 mat4_scale(double sx, double sy, double sz);
Mat4 mat4_rotate_x(double rad);
Mat4 mat4_rotate_y(double rad);
Mat4 mat4_rotate_z(double rad);
Mat4 mat4_rotate_axis(const Vec3& axis, double rad);

// 组合变换：平移 t、旋转角（欧拉角，弧度，依次 X、Y、Z）、缩放 s
Mat4 mat4_transform(const Vec3& t, const Vec3& euler_xyz, const Vec3& s);

// ---- 投影矩阵 ----
// 透视投影：fovy 弧度，aspect = w/h，近远平面 n,f（>0，相机朝 -Z）
Mat4 mat4_perspective(double fovy, double aspect, double znear, double zfar);
// 正交投影（长方体盒）
Mat4 mat4_ortho(double left, double right, double bottom, double top,
                double znear, double zfar);
// 透视投影（对称盒）
Mat4 mat4_ortho_center(double half_w, double half_h, double znear, double zfar);

// ---- 视图矩阵（lookAt）----
// eye 相机位置，target 注视点，up 世界上方向
Mat4 mat4_look_at(const Vec3& eye, const Vec3& target, const Vec3& up);

// ============================================================================
// Quat —— 四元数（单位四元数表示旋转）
// ============================================================================
struct Quat {
    double w, x, y, z;   // w 实部，(x,y,z) 虚部
    Quat() : w(1), x(0), y(0), z(0) {}
    Quat(double w_, double x_, double y_, double z_)
        : w(w_), x(x_), y(y_), z(z_) {}

    // 绕轴旋转构造（axis 单位化，rad 弧度）
    static Quat from_axis_angle(const Vec3& axis, double rad);
    // 欧拉角（弧度，YXZ 顺序，常用飞行相机）
    static Quat from_euler(double pitch, double yaw, double roll);
    void to_axis_angle(Vec3& axis, double& rad) const;
    Mat4  to_mat4() const;
    Mat3  to_mat3() const;

    Quat operator*(const Quat& o) const;     // 旋转复合
    Vec3 operator*(const Vec3& v) const;   // 用四元数旋转向量
    Quat conjugate() const { return Quat(w, -x, -y, -z); }
    double length_sq() const { return w * w + x * x + y * y + z * z; }
    double length() const;
    Quat normalized() const;
    Quat inverse() const;                   // 单位四元数的逆 = 共轭
};
Quat slerp(const Quat& a, const Quat& b, double t);   // 球面线性插值

// ============================================================================
// 几何图元
// ============================================================================

// 平面：n·p + d = 0（n 单位法线）
struct Plane {
    Vec3 n;     // 单位法线
    double d;   // 偏移
    Plane() : d(0) {}
    Plane(const Vec3& n_, double d_) : n(n_), d(d_) {}
    // 过三点构造（逆时针绕序朝外）
    static Plane from_points(const Vec3& a, const Vec3& b, const Vec3& c);
    double signed_distance(const Vec3& p) const { return n.dot(p) + d; }
};

// 射线：origin + t * dir
struct Ray {
    Vec3 origin;
    Vec3 dir;   // 单位化
    Ray() {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), dir(d.normalized()) {}
    Vec3 point_at(double t) const { return origin + dir * t; }
};

// 轴对齐包围盒
struct AABB {
    Vec3 mn, mx;
    AABB() : mn(1e30, 1e30, 1e30), mx(-1e30, -1e30, -1e30) {}
    AABB(const Vec3& a, const Vec3& b) : mn(a), mx(b) {}
    void expand(const Vec3& p) {
        if (p.x < mn.x) mn.x = p.x;  if (p.x > mx.x) mx.x = p.x;
        if (p.y < mn.y) mn.y = p.y;  if (p.y > mx.y) mx.y = p.y;
        if (p.z < mn.z) mn.z = p.z;  if (p.z > mx.z) mx.z = p.z;
    }
    void expand(const AABB& b) { expand(b.mn); expand(b.mx); }
    Vec3 center() const { return (mn + mx) * 0.5; }
    Vec3 extent() const { return (mx - mn) * 0.5; }
    bool contains(const Vec3& p) const {
        return p.x >= mn.x && p.x <= mx.x &&
               p.y >= mn.y && p.y <= mx.y &&
               p.z >= mn.z && p.z <= mx.z;
    }
};

// 球体
struct Sphere {
    Vec3 c;
    double r;
    Sphere() : r(0) {}
    Sphere(const Vec3& c_, double r_) : c(c_), r(r_) {}
    bool contains(const Vec3& p) const { return (p - c).length_sq() <= r * r; }
};

// 有向包围盒（局部中心 + 局部半尺寸 + 旋转矩阵）
struct OBB {
    Vec3 center;
    Vec3 half;       // 局部 X/Y/Z 半尺寸
    Mat3 rot;        // 局部 -> 世界
};

// ============================================================================
// 相交测试（返回命中距离 t，未命中返回 -1）
// ============================================================================
double ray_sphere(const Ray& ray, const Sphere& s);
double ray_plane(const Ray& ray, const Plane& p);
// Möller–Trumbore 三角形求交
double ray_triangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c,
                    double& u, double& v);
// slab 法射线-AABB，返回近交点 tnear；未命中返回 -1
double ray_aabb(const Ray& ray, const AABB& box);

// ---- 图元间相交 ----
bool aabb_intersects(const AABB& a, const AABB& b);
bool sphere_intersects(const Sphere& a, const Sphere& b);
bool sphere_aabb(const Sphere& s, const AABB& b);
// 两平面交线（返回是否相交，line 存原点+方向）
bool plane_plane_intersect(const Plane& p, const Plane& q, Ray& line);

// 四元数与欧拉角互转
Vec3 quat_to_euler(const Quat& q);
Quat  quat_from_mat3(const Mat3& m);

// 视锥体（由 6 个平面组成，用于视锥剔除）
struct Frustum {
    Plane planes[6];   // 左 右 上 下 近 远
    // 从 projection*view 矩阵提取（Gribb–Hartmann 法）
    static Frustum from_matrix(const Mat4& proj_view);
    bool intersects(const AABB& box) const;
    bool intersects(const Sphere& s) const;
};

// 四元数球面线性插值（SLERP）
Quat quat_slerp(const Quat& a, const Quat& b, double t);
// 从两个方向向量构造旋转
Quat quat_from_directions(const Vec3& from, const Vec3& to);
// 矩阵求逆（Mat4 一般矩阵）
Mat4 mat4_inverse(const Mat4& m);
// Catmull-Rom 样条插值
Vec3 catmull_rom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t);

// ============================================================================
// self test
// ============================================================================
// 运行 math3d 全部断言；返回失败条数（0 == 全绿）
int math3d_self_test();

} // namespace gfx3d
} // namespace nefu
