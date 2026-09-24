// ============================================================================
// nefuOS 3D 图形库 —— math3d 实现
// ============================================================================
#include "math3d.h"

#include <cmath>
#include <cstring>

namespace nefu {
namespace gfx3d {

// 本文件内部使用的数学函数别名（host 构建直接用 libm）
static inline double f_abs(double x) { return x < 0 ? -x : x; }
static inline double f_sqrt(double x) { return std::sqrt(x); }
static inline double f_sin(double x) { return std::sin(x); }
static inline double f_cos(double x) { return std::cos(x); }

// ============================================================================
// Vec2 实现
// ============================================================================
double Vec2::length() const { return f_sqrt(length_sq()); }

Vec2 Vec2::normalized() const {
    double l = length();
    if (l < EPS_D) return Vec2(0, 0);
    return Vec2(x / l, y / l);
}

Vec2 lerp(const Vec2& a, const Vec2& b, double t) {
    // 线性插值：(1-t)*a + t*b
    return Vec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

// ============================================================================
// Vec3 实现
// ============================================================================
double Vec3::length() const { return f_sqrt(length_sq()); }

Vec3 Vec3::normalized() const {
    double l = length();
    if (l < EPS_D) return Vec3(0, 0, 0);
    return Vec3(x / l, y / l, z / l);
}

Vec3 lerp(const Vec3& a, const Vec3& b, double t) {
    return Vec3(a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t);
}

Vec3 reflect(const Vec3& I, const Vec3& N) {
    // R = I - 2*(I·N)*N
    return I - N * (2.0 * I.dot(N));
}

Vec3 half_vector(const Vec3& L, const Vec3& V) {
    // Blinn-Phong 半程向量：H = normalize(L + V)
    return (L + V).normalized();
}

// ============================================================================
// Mat2 实现
// ============================================================================
Mat2::Mat2() {
    m[0][0] = 1; m[0][1] = 0;
    m[1][0] = 0; m[1][1] = 1;
}
Mat2 Mat2::identity() { return Mat2(); }
Mat2 Mat2::zero() {
    Mat2 r;
    r.m[0][0] = 0; r.m[0][1] = 0;
    r.m[1][0] = 0; r.m[1][1] = 0;
    return r;
}

Mat2 operator*(const Mat2& A, const Mat2& B) {
    Mat2 R;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            double s = 0;
            for (int k = 0; k < 2; k++) s += A.m[i][k] * B.m[k][j];
            R.m[i][j] = s;
        }
    return R;
}

Vec2 operator*(const Mat2& M, const Vec2& v) {
    return Vec2(M.m[0][0] * v.x + M.m[0][1] * v.y,
                M.m[1][0] * v.x + M.m[1][1] * v.y);
}

Mat2 transpose(const Mat2& M) {
    Mat2 R;
    R.m[0][1] = M.m[1][0];
    R.m[1][0] = M.m[0][1];
    return R;
}

double determinant(const Mat2& M) {
    return M.m[0][0] * M.m[1][1] - M.m[0][1] * M.m[1][0];
}

Mat2 inverse(const Mat2& M) {
    double det = determinant(M);
    if (f_abs(det) < EPS_D) return Mat2::zero();
    double inv = 1.0 / det;
    Mat2 R;
    R.m[0][0] =  M.m[1][1] * inv;
    R.m[0][1] = -M.m[0][1] * inv;
    R.m[1][0] = -M.m[1][0] * inv;
    R.m[1][1] =  M.m[0][0] * inv;
    return R;
}

// ============================================================================
// Mat3 实现
// ============================================================================
Mat3::Mat3() {
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            m[i][j] = (i == j) ? 1.0 : 0.0;
}
Mat3 Mat3::identity() { return Mat3(); }
Mat3 Mat3::zero() {
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = 0;
    return r;
}

Mat3 operator*(const Mat3& A, const Mat3& B) {
    Mat3 R;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++) s += A.m[i][k] * B.m[k][j];
            R.m[i][j] = s;
        }
    return R;
}

Vec3 operator*(const Mat3& M, const Vec3& v) {
    return Vec3(M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z,
                M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z,
                M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z);
}

Mat3 transpose(const Mat3& M) {
    Mat3 R;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) R.m[i][j] = M.m[j][i];
    return R;
}

double determinant(const Mat3& M) {
    return M.m[0][0] * (M.m[1][1] * M.m[2][2] - M.m[1][2] * M.m[2][1])
         - M.m[0][1] * (M.m[1][0] * M.m[2][2] - M.m[1][2] * M.m[2][0])
         + M.m[0][2] * (M.m[1][0] * M.m[2][1] - M.m[1][1] * M.m[2][0]);
}

Mat3 inverse(const Mat3& M) {
    // 伴随矩阵法求逆：inv = adj / det
    double a00 = M.m[0][0], a01 = M.m[0][1], a02 = M.m[0][2];
    double a10 = M.m[1][0], a11 = M.m[1][1], a12 = M.m[1][2];
    double a20 = M.m[2][0], a21 = M.m[2][1], a22 = M.m[2][2];

    double b00 = a11 * a22 - a12 * a21;
    double b01 = a12 * a20 - a10 * a22;
    double b02 = a10 * a21 - a11 * a20;
    double det = a00 * b00 + a01 * b01 + a02 * b02;
    if (f_abs(det) < EPS_D) return Mat3::zero();
    double inv = 1.0 / det;

    Mat3 R;
    R.m[0][0] = b00 * inv;
    R.m[0][1] = (a02 * a21 - a01 * a22) * inv;
    R.m[0][2] = (a01 * a12 - a02 * a11) * inv;
    R.m[1][0] = b01 * inv;
    R.m[1][1] = (a00 * a22 - a02 * a20) * inv;
    R.m[1][2] = (a02 * a10 - a00 * a12) * inv;
    R.m[2][0] = b02 * inv;
    R.m[2][1] = (a01 * a20 - a00 * a21) * inv;
    R.m[2][2] = (a00 * a11 - a01 * a10) * inv;
    return R;
}

Mat3 mat3_rotate_axis(const Vec3& axis, double rad) {
    // Rodrigues 旋转公式：R = cosθ I + (1-cosθ) k k^T + sinθ [k]_x
    Vec3 k = axis.normalized();
    double c = f_cos(rad), s = f_sin(rad), t = 1.0 - c;
    Mat3 R;
    R.m[0][0] = t * k.x * k.x + c;
    R.m[0][1] = t * k.x * k.y - s * k.z;
    R.m[0][2] = t * k.x * k.z + s * k.y;
    R.m[1][0] = t * k.x * k.y + s * k.z;
    R.m[1][1] = t * k.y * k.y + c;
    R.m[1][2] = t * k.y * k.z - s * k.x;
    R.m[2][0] = t * k.x * k.z - s * k.y;
    R.m[2][1] = t * k.y * k.z + s * k.x;
    R.m[2][2] = t * k.z * k.z + c;
    return R;
}

// ============================================================================
// Mat4 实现
// ============================================================================
Mat4::Mat4() {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            m[i][j] = (i == j) ? 1.0 : 0.0;
}
Mat4 Mat4::identity() { return Mat4(); }
Mat4 Mat4::zero() {
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r.m[i][j] = 0;
    return r;
}

Mat4 operator*(const Mat4& A, const Mat4& B) {
    Mat4 R;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += A.m[i][k] * B.m[k][j];
            R.m[i][j] = s;
        }
    return R;
}

Vec4 operator*(const Mat4& M, const Vec4& v) {
    return Vec4(
        M.m[0][0] * v.x + M.m[0][1] * v.y + M.m[0][2] * v.z + M.m[0][3] * v.w,
        M.m[1][0] * v.x + M.m[1][1] * v.y + M.m[1][2] * v.z + M.m[1][3] * v.w,
        M.m[2][0] * v.x + M.m[2][1] * v.y + M.m[2][2] * v.z + M.m[2][3] * v.w,
        M.m[3][0] * v.x + M.m[3][1] * v.y + M.m[3][2] * v.z + M.m[3][3] * v.w);
}

Mat4 transpose(const Mat4& M) {
    Mat4 R;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) R.m[i][j] = M.m[j][i];
    return R;
}

// 4x4 行列式（按最后一行展开，最后一行多为 [0 0 0 1] 形式时退化为 3x3）
double determinant(const Mat4& M) {
    double det = 0;
    for (int i = 0; i < 4; i++) {
        // 余子式：去掉第 0 行、第 i 列后的 3x3
        Mat3 sub;
        int ci = 0;
        for (int r = 1; r < 4; r++) {
            int cj = 0;
            for (int c = 0; c < 4; c++) {
                if (c == i) continue;
                sub.m[ci][cj] = M.m[r][c];
                cj++;
            }
            ci++;
        }
        double sign = ((i % 2) == 0) ? 1.0 : -1.0;
        det += sign * M.m[0][i] * determinant(sub);
    }
    return det;
}

Mat4 inverse(const Mat4& M) {
    // Gauss-Jordan 求逆：把 [M | I] 行变换成 [I | inv(M)]
    Mat4 A = M, R;
    // R 初始化为单位矩阵
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) R.m[i][j] = (i == j) ? 1.0 : 0.0;

    for (int col = 0; col < 4; col++) {
        // 选主元
        int piv = col;
        for (int r = col + 1; r < 4; r++)
            if (f_abs(A.m[r][col]) > f_abs(A.m[piv][col])) piv = r;
        if (f_abs(A.m[piv][col]) < EPS_D) return Mat4::zero();  // 奇异
        if (piv != col) {
            for (int c = 0; c < 4; c++) {
                double t;
                t = A.m[col][c]; A.m[col][c] = A.m[piv][c]; A.m[piv][c] = t;
                t = R.m[col][c]; R.m[col][c] = R.m[piv][c]; R.m[piv][c] = t;
            }
        }
        double d = A.m[col][col];
        for (int c = 0; c < 4; c++) {
            A.m[col][c] /= d;
            R.m[col][c] /= d;
        }
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = A.m[r][col];
            for (int c = 0; c < 4; c++) {
                A.m[r][c] -= f * A.m[col][c];
                R.m[r][c] -= f * R.m[col][c];
            }
        }
    }
    return R;
}

// ---- 模型变换矩阵 ----
Mat4 mat4_translate(double x, double y, double z) {
    Mat4 R;
    R.m[0][3] = x;
    R.m[1][3] = y;
    R.m[2][3] = z;
    return R;
}

Mat4 mat4_scale(double sx, double sy, double sz) {
    Mat4 R;
    R.m[0][0] = sx;
    R.m[1][1] = sy;
    R.m[2][2] = sz;
    return R;
}

Mat4 mat4_rotate_x(double rad) {
    Mat4 R;
    double c = f_cos(rad), s = f_sin(rad);
    R.m[1][1] =  c; R.m[1][2] = -s;
    R.m[2][1] =  s; R.m[2][2] =  c;
    return R;
}

Mat4 mat4_rotate_y(double rad) {
    Mat4 R;
    double c = f_cos(rad), s = f_sin(rad);
    R.m[0][0] =  c; R.m[0][2] = s;
    R.m[2][0] = -s; R.m[2][2] = c;
    return R;
}

Mat4 mat4_rotate_z(double rad) {
    Mat4 R;
    double c = f_cos(rad), s = f_sin(rad);
    R.m[0][0] =  c; R.m[0][1] = -s;
    R.m[1][0] =  s; R.m[1][1] =  c;
    return R;
}

Mat4 mat4_rotate_axis(const Vec3& axis, double rad) {
    Mat4 R;
    Mat3 r = mat3_rotate_axis(axis, rad);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) R.m[i][j] = r.m[i][j];
    return R;
}

Mat4 mat4_transform(const Vec3& t, const Vec3& euler_xyz, const Vec3& s) {
    // M = T * Rx * Ry * Rz * S
    Mat4 M = mat4_translate(t.x, t.y, t.z);
    M = M * mat4_rotate_x(euler_xyz.x);
    M = M * mat4_rotate_y(euler_xyz.y);
    M = M * mat4_rotate_z(euler_xyz.z);
    M = M * mat4_scale(s.x, s.y, s.z);
    return M;
}

// ---- 投影矩阵 ----
Mat4 mat4_perspective(double fovy, double aspect, double znear, double zfar) {
    // OpenGL 风格透视投影，NDC z 范围 [-1,1]
    double tan_half = f_sin(fovy * 0.5) / f_cos(fovy * 0.5);  // tan(fovy/2)
    Mat4 R = Mat4::zero();
    R.m[0][0] = 1.0 / (aspect * tan_half);
    R.m[1][1] = 1.0 / tan_half;
    R.m[2][2] = -(zfar + znear) / (zfar - znear);
    R.m[2][3] = -(2.0 * zfar * znear) / (zfar - znear);
    R.m[3][2] = -1.0;
    return R;
}

Mat4 mat4_ortho(double left, double right, double bottom, double top,
                double znear, double zfar) {
    Mat4 R;
    R.m[0][0] = 2.0 / (right - left);
    R.m[1][1] = 2.0 / (top - bottom);
    R.m[2][2] = -2.0 / (zfar - znear);
    R.m[0][3] = -(right + left) / (right - left);
    R.m[1][3] = -(top + bottom) / (top - bottom);
    R.m[2][3] = -(zfar + znear) / (zfar - znear);
    // m[3][3] 已为 1
    return R;
}

Mat4 mat4_ortho_center(double half_w, double half_h, double znear, double zfar) {
    return mat4_ortho(-half_w, half_w, -half_h, half_h, znear, zfar);
}

Mat4 mat4_look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
    // 右手系相机：z 轴从 target 指向 eye
    Vec3 z = (eye - target).normalized();
    Vec3 x = up.cross(z).normalized();
    Vec3 y = z.cross(x);
    Mat4 R;
    R.m[0][0] = x.x; R.m[0][1] = x.y; R.m[0][2] = x.z; R.m[0][3] = -x.dot(eye);
    R.m[1][0] = y.x; R.m[1][1] = y.y; R.m[1][2] = y.z; R.m[1][3] = -y.dot(eye);
    R.m[2][0] = z.x; R.m[2][1] = z.y; R.m[2][2] = z.z; R.m[2][3] = -z.dot(eye);
    R.m[3][3] = 1.0;
    return R;
}

// ============================================================================
// Quat 实现
// ============================================================================
double Quat::length() const { return f_sqrt(length_sq()); }

Quat Quat::normalized() const {
    double l = length();
    if (l < EPS_D) return Quat(1, 0, 0, 0);
    return Quat(w / l, x / l, y / l, z / l);
}

Quat Quat::inverse() const {
    // 单位四元数：q^-1 = conj(q)
    return conjugate().normalized();
}

Quat Quat::from_axis_angle(const Vec3& axis, double rad) {
    Vec3 k = axis.normalized();
    double half = rad * 0.5;
    double s = f_sin(half);
    return Quat(f_cos(half), k.x * s, k.y * s, k.z * s);
}

Quat Quat::from_euler(double pitch, double yaw, double roll) {
    // 常用 YXZ 顺序：先偏航 yaw(Y)，再俯仰 pitch(X)，最后滚转 roll(Z)
    Quat qy = from_axis_angle(Vec3(0, 1, 0), yaw);
    Quat qx = from_axis_angle(Vec3(1, 0, 0), pitch);
    Quat qz = from_axis_angle(Vec3(0, 0, 1), roll);
    return (qy * qx * qz).normalized();
}

void Quat::to_axis_angle(Vec3& axis, double& rad) const {
    double w_ = w > 1 ? 1 : (w < -1 ? -1 : w);
    double angle = 2.0 * std::acos(w_);
    double s = f_sqrt(1.0 - w_ * w_);
    if (s < EPS_D) { axis = Vec3(1, 0, 0); rad = 0; return; }
    axis = Vec3(x / s, y / s, z / s);
    rad = angle;
}

Mat4 Quat::to_mat4() const {
    Mat4 R;
    double xx = x * x, yy = y * y, zz = z * z;
    double xy = x * y, xz = x * z, yz = y * z;
    double wx = w * x, wy = w * y, wz = w * z;
    R.m[0][0] = 1 - 2 * (yy + zz);
    R.m[0][1] = 2 * (xy - wz);
    R.m[0][2] = 2 * (xz + wy);
    R.m[1][0] = 2 * (xy + wz);
    R.m[1][1] = 1 - 2 * (xx + zz);
    R.m[1][2] = 2 * (yz - wx);
    R.m[2][0] = 2 * (xz - wy);
    R.m[2][1] = 2 * (yz + wx);
    R.m[2][2] = 1 - 2 * (xx + yy);
    return R;
}

Mat3 Quat::to_mat3() const {
    Mat3 R;
    double xx = x * x, yy = y * y, zz = z * z;
    double xy = x * y, xz = x * z, yz = y * z;
    double wx = w * x, wy = w * y, wz = w * z;
    R.m[0][0] = 1 - 2 * (yy + zz);
    R.m[0][1] = 2 * (xy - wz);
    R.m[0][2] = 2 * (xz + wy);
    R.m[1][0] = 2 * (xy + wz);
    R.m[1][1] = 1 - 2 * (xx + zz);
    R.m[1][2] = 2 * (yz - wx);
    R.m[2][0] = 2 * (xz - wy);
    R.m[2][1] = 2 * (yz + wx);
    R.m[2][2] = 1 - 2 * (xx + yy);
    return R;
}

Quat Quat::operator*(const Quat& o) const {
    // Hamilton 积：q1*q2 表示先 q2 后 q1 的旋转
    return Quat(
        w * o.w - x * o.x - y * o.y - z * o.z,
        w * o.x + x * o.w + y * o.z - z * o.y,
        w * o.y - x * o.z + y * o.w + z * o.x,
        w * o.z + x * o.y - y * o.x + z * o.w);
}

Vec3 Quat::operator*(const Vec3& v) const {
    // v' = q * v * q^-1（只取旋转部分，快路径）
    Vec3 u = Vec3(x, y, z);
    double s = w;
    return u * (2.0 * u.dot(v)) + v * (s * s - u.dot(u)) + u.cross(v) * (2.0 * s);
}

Quat slerp(const Quat& a, const Quat& b, double t) {
    // 球面线性插值：保证旋转角速度恒定
    Quat b2 = b;
    double cos_h = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    if (cos_h < 0) { cos_h = -cos_h; b2 = Quat(-b.w, -b.x, -b.y, -b.z); }
    if (cos_h > 0.9995) {
        // 非常接近，退化为线性插值避免除零
        Quat r = Quat(a.w + t * (b2.w - a.w),
                      a.x + t * (b2.x - a.x),
                      a.y + t * (b2.y - a.y),
                      a.z + t * (b2.z - a.z));
        return r.normalized();
    }
    double h = std::acos(cos_h);
    double sh = f_sqrt(1.0 - cos_h * cos_h);
    double wa = f_sin((1 - t) * h) / sh;
    double wb = f_sin(t * h) / sh;
    return Quat(wa * a.w + wb * b2.w,
                wa * a.x + wb * b2.x,
                wa * a.y + wb * b2.y,
                wa * a.z + wb * b2.z);
}

// ============================================================================
// 平面
// ============================================================================
Plane Plane::from_points(const Vec3& a, const Vec3& b, const Vec3& c) {
    Vec3 n = (b - a).cross(c - a).normalized();
    Plane p;
    p.n = n;
    p.d = -n.dot(a);
    return p;
}

// ============================================================================
// 相交测试
// ============================================================================
double ray_sphere(const Ray& ray, const Sphere& s) {
    // 解 |o + t d - c|^2 = r^2
    Vec3 oc = ray.origin - s.c;
    double b = oc.dot(ray.dir);
    double c = oc.dot(oc) - s.r * s.r;
    double disc = b * b - c;
    if (disc < 0) return -1;
    double t = -b - f_sqrt(disc);
    if (t < 0) t = -b + f_sqrt(disc);   // 射线起点在球内时取远交点
    return t >= 0 ? t : -1;
}

double ray_plane(const Ray& ray, const Plane& p) {
    // n·(o + t d) + d = 0
    double denom = p.n.dot(ray.dir);
    if (f_abs(denom) < EPS_D) return -1;
    double t = -(p.n.dot(ray.origin) + p.d) / denom;
    return t >= 0 ? t : -1;
}

double ray_triangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c,
                    double& u, double& v) {
    // Möller–Trumbore 算法
    Vec3 ab = b - a;
    Vec3 ac = c - a;
    Vec3 p = ray.dir.cross(ac);
    double det = ab.dot(p);
    if (f_abs(det) < EPS_D) return -1;
    double inv_det = 1.0 / det;
    Vec3 t = ray.origin - a;
    u = t.dot(p) * inv_det;
    if (u < 0 || u > 1) return -1;
    Vec3 q = t.cross(ab);
    v = ray.dir.dot(q) * inv_det;
    if (v < 0 || u + v > 1) return -1;
    double dist = ac.dot(q) * inv_det;
    return dist > EPS_D ? dist : -1;
}

double ray_aabb(const Ray& ray, const AABB& box) {
    // slab 法：逐轴求交区间，取最大近点、最小远点
    double tmin = 0.0, tmax = 1e30;
    for (int i = 0; i < 3; i++) {
        double o = i == 0 ? ray.origin.x : (i == 1 ? ray.origin.y : ray.origin.z);
        double d = i == 0 ? ray.dir.x : (i == 1 ? ray.dir.y : ray.dir.z);
        double mn = i == 0 ? box.mn.x : (i == 1 ? box.mn.y : box.mn.z);
        double mx = i == 0 ? box.mx.x : (i == 1 ? box.mx.y : box.mx.z);
        if (f_abs(d) < EPS_D) {
            if (o < mn || o > mx) return -1;
        } else {
            double t1 = (mn - o) / d;
            double t2 = (mx - o) / d;
            if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return -1;
        }
    }
    return tmin;
}

// ============================================================================
// 视锥体（Gribb–Hartmann 提取）
// ============================================================================
Frustum Frustum::from_matrix(const Mat4& pv) {
    // 行主序矩阵 M[行][列]，clip 空间裁剪平面：±w 与各行的线性组合
    Frustum f;
    for (int i = 0; i < 6; i++) {
        int r = i / 2;          // 0:x  1:y  2:z  3:w
        int s = (i % 2 == 0) ? +1 : -1;
        Plane& p = f.planes[i];
        p.n.x = pv.m[0][3] + s * pv.m[0][r];
        p.n.y = pv.m[1][3] + s * pv.m[1][r];
        p.n.z = pv.m[2][3] + s * pv.m[2][r];
        p.d   = pv.m[3][3] + s * pv.m[3][r];
        double l = p.n.length();
        if (l > EPS_D) { p.n = p.n / l; p.d /= l; }
    }
    return f;
}

bool Frustum::intersects(const AABB& box) const {
    // 每个平面取"最靠平面内侧"的角点测试
    for (int i = 0; i < 6; i++) {
        const Plane& p = planes[i];
        Vec3 px(box.mn.x, box.mn.y, box.mn.z);
        if (p.n.x > 0) px.x = box.mx.x;
        if (p.n.y > 0) px.y = box.mx.y;
        if (p.n.z > 0) px.z = box.mx.z;
        if (p.signed_distance(px) < 0) return false;   // 完全在外
    }
    return true;
}

bool Frustum::intersects(const Sphere& s) const {
    for (int i = 0; i < 6; i++) {
        if (planes[i].signed_distance(s.c) < -s.r) return false;
    }
    return true;
}

// ============================================================================
// 图元间相交
// ============================================================================
bool aabb_intersects(const AABB& a, const AABB& b) {
    return a.mn.x <= b.mx.x && a.mx.x >= b.mn.x &&
           a.mn.y <= b.mx.y && a.mx.y >= b.mn.y &&
           a.mn.z <= b.mx.z && a.mx.z >= b.mn.z;
}

bool sphere_intersects(const Sphere& a, const Sphere& b) {
    double r = a.r + b.r;
    return (a.c - b.c).length_sq() <= r * r;
}

bool sphere_aabb(const Sphere& s, const AABB& b) {
    // 找 AABB 上离球心最近的点
    Vec3 closest;
    closest.x = s.c.x < b.mn.x ? b.mn.x : (s.c.x > b.mx.x ? b.mx.x : s.c.x);
    closest.y = s.c.y < b.mn.y ? b.mn.y : (s.c.y > b.mx.y ? b.mx.y : s.c.y);
    closest.z = s.c.z < b.mn.z ? b.mn.z : (s.c.z > b.mx.z ? b.mx.z : s.c.z);
    return (closest - s.c).length_sq() <= s.r * s.r;
}

bool plane_plane_intersect(const Plane& p, const Plane& q, Ray& line) {
    Vec3 d = p.n.cross(q.n);
    if (d.length() < EPS_D) return false;    // 平行
    // 解：找一个点在两个平面上
    double det = d.length_sq();
    Vec3 p0 = (q.n * p.d - p.n * q.d).cross(d) / det;
    line.origin = p0;
    line.dir = d.normalized();
    return true;
}

Vec3 quat_to_euler(const Quat& q) {
    // YXZ 顺序
    double sinr_cosp = 2 * (q.w * q.x + q.y * q.z);
    double cosr_cosp = 1 - 2 * (q.x * q.x + q.y * q.y);
    double roll = std::atan2(sinr_cosp, cosr_cosp);

    double sinp = 2 * (q.w * q.y - q.z * q.x);
    double pitch = (f_abs(sinp) >= 1) ? (sinp > 0 ? PI_D / 2 : -PI_D / 2)
                                       : std::asin(sinp);

    double siny_cosp = 2 * (q.w * q.z + q.x * q.y);
    double cosy_cosp = 1 - 2 * (q.y * q.y + q.z * q.z);
    double yaw = std::atan2(siny_cosp, cosy_cosp);
    return Vec3(pitch, yaw, roll);
}

Quat quat_from_mat3(const Mat3& m) {
    double tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
    Quat q;
    if (tr > 0) {
        double s = std::sqrt(tr + 1.0) * 2;
        q.w = 0.25 * s;
        q.x = (m.m[2][1] - m.m[1][2]) / s;
        q.y = (m.m[0][2] - m.m[2][0]) / s;
        q.z = (m.m[1][0] - m.m[0][1]) / s;
    } else {
        // 选对角最大的轴
        if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
            double s = std::sqrt(1.0 + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2;
            q.w = (m.m[2][1] - m.m[1][2]) / s;
            q.x = 0.25 * s;
            q.y = (m.m[0][1] + m.m[1][0]) / s;
            q.z = (m.m[0][2] + m.m[2][0]) / s;
        } else if (m.m[1][1] > m.m[2][2]) {
            double s = std::sqrt(1.0 + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2;
            q.w = (m.m[0][2] - m.m[2][0]) / s;
            q.x = (m.m[0][1] + m.m[1][0]) / s;
            q.y = 0.25 * s;
            q.z = (m.m[1][2] + m.m[2][1]) / s;
        } else {
            double s = std::sqrt(1.0 + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2;
            q.w = (m.m[1][0] - m.m[0][1]) / s;
            q.x = (m.m[0][2] + m.m[2][0]) / s;
            q.y = (m.m[1][2] + m.m[2][1]) / s;
            q.z = 0.25 * s;
        }
    }
    return q.normalized();
}

// ============================================================================
// self test
// ============================================================================
static bool near(double a, double b, double eps = 1e-6) {
    return f_abs(a - b) < eps;
}

// ============================================================================
// 四元数 SLERP 与矩阵求逆
// ============================================================================
Quat quat_slerp(const Quat& a, const Quat& b, double t) {
    double cosine = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    Quat b1 = b;
    if (cosine < 0) { cosine = -cosine; b1 = Quat(-b.w, -b.x, -b.y, -b.z); }
    if (cosine > 0.9995) {
        return Quat(a.w + t * (b1.w - a.w),
                    a.x + t * (b1.x - a.x),
                    a.y + t * (b1.y - a.y),
                    a.z + t * (b1.z - a.z)).normalized();
    }
    double theta0 = acos(cosine);
    double theta = theta0 * t;
    Quat rel(b1.w - cosine * a.w, b1.x - cosine * a.x,
             b1.y - cosine * a.y, b1.z - cosine * a.z);
    rel = rel.normalized();
    return Quat(a.w*cos(theta)+rel.w*sin(theta), a.x*cos(theta)+rel.x*sin(theta), a.y*cos(theta)+rel.y*sin(theta), a.z*cos(theta)+rel.z*sin(theta));
}

Quat quat_from_directions(const Vec3& from, const Vec3& to) {
    Vec3 f = from.normalized(), t = to.normalized();
    double d = f.dot(t);
    if (d > 0.9999) return Quat(1, 0, 0, 0);
    Vec3 axis = f.cross(t);
    double s = sqrt((1 + d) * 2);
    return Quat(s * 0.5, axis.x / s, axis.y / s, axis.z / s);
}

Mat4 mat4_inverse(const Mat4& m) {
    double aug[4][8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) { aug[i][j] = m.m[i][j]; aug[i][j+4] = (i==j)?1.0:0.0; }
    }
    for (int col = 0; col < 4; col++) {
        int pivot = col;
        for (int r = col+1; r < 4; r++)
            if (fabs(aug[r][col]) > fabs(aug[pivot][col])) pivot = r;
        if (pivot != col)
            for (int j = 0; j < 8; j++) { double t = aug[col][j]; aug[col][j] = aug[pivot][j]; aug[pivot][j] = t; }
        double div = aug[col][col];
        for (int j = 0; j < 8; j++) aug[col][j] /= div;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double factor = aug[r][col];
            for (int j = 0; j < 8; j++) aug[r][j] -= factor * aug[col][j];
        }
    }
    Mat4 out;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) out.m[i][j] = aug[i][j+4];
    return out;
}

Vec3 catmull_rom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t) {
    double t2 = t * t, t3 = t2 * t;
    return p1 * 0.5 +
        (p2 - p0) * 0.5 * t +
        (p0 * 2.0 - p1 * 5.0 + p2 * 4.0 - p3) * 0.5 * t2 +
        (-p0 + p1 * 3.0 - p2 * 3.0 + p3) * 0.5 * t3;
}

int math3d_self_test() {
    int fail = 0;

    // --- Vec3 基础 ---
    {
        Vec3 a(1, 2, 3), b(4, 5, 6);
        Vec3 c = a + b;
        if (!near(c.x, 5) || !near(c.y, 7) || !near(c.z, 9)) fail++;
        if (!near(a.dot(b), 32)) fail++;            // 1*4+2*5+3*6=32
        Vec3 cr = a.cross(b);
        // (1,2,3)x(4,5,6) = (2*6-3*5, 3*4-1*6, 1*5-2*4) = (-3,6,-3)
        if (!near(cr.x, -3) || !near(cr.y, 6) || !near(cr.z, -3)) fail++;
        Vec3 n = Vec3(3, 0, 0).normalized();
        if (!near(n.x, 1) || !near(n.y, 0)) fail++;
    }

    // --- Mat4 单位矩阵 ---
    {
        Mat4 I;
        Mat4 A = mat4_translate(1, 2, 3);
        Mat4 R = I * A;
        // 单位矩阵乘 A 应等于 A
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (!near(R.m[i][j], A.m[i][j])) fail++;
        // 向量经过 I 不变
        Vec4 v(1, 2, 3, 1);
        Vec4 rv = I * v;
        if (!near(rv.x, 1) || !near(rv.y, 2) || !near(rv.z, 3)) fail++;
    }

    // --- 旋转 90 度矩阵作用于 (1,0,0) 得 (0,1,0) ---
    {
        Mat4 R = mat4_rotate_z(deg2rad(90.0));
        Vec4 v = R * Vec4(1, 0, 0, 1);
        // 绕 Z 轴 +90°：x->y
        if (!near(v.x, 0, 1e-6) || !near(v.y, 1.0, 1e-6) || !near(v.z, 0)) fail++;

        // 绕 X 轴 90°：(0,1,0) -> (0,0,1)
        Mat4 Rx = mat4_rotate_x(deg2rad(90.0));
        Vec4 v2 = Rx * Vec4(0, 1, 0, 1);
        if (!near(v2.y, 0, 1e-6) || !near(v2.z, 1.0, 1e-6)) fail++;
    }

    // --- 平移矩阵 ---
    {
        Mat4 T = mat4_translate(5, 6, 7);
        Vec4 v = T * Vec4(1, 1, 1, 1);
        if (!near(v.x, 6) || !near(v.y, 7) || !near(v.z, 8)) fail++;
    }

    // --- 透视投影验证：近平面 z=-1 在 NDC z=-1，远平面 z=-100 在 NDC z=+1 ---
    {
        Mat4 P = mat4_perspective(deg2rad(60.0), 4.0 / 3.0, 1.0, 100.0);
        Vec4 near_p = P * Vec4(0, 0, -1.0, 1.0);
        Vec4 far_p  = P * Vec4(0, 0, -100.0, 1.0);
        // near: w = 1，z/w 应为 -1
        double nz = near_p.z / near_p.w;
        double fz = far_p.z / far_p.w;
        if (!near(nz, -1.0, 1e-5)) fail++;
        if (!near(fz,  1.0, 1e-5)) fail++;
        // x=0,y=0 投影后应为 0
        if (!near(near_p.x / near_p.w, 0)) fail++;
    }

    // --- lookAt：相机在 (0,0,5) 看向原点 ---
    {
        Mat4 V = mat4_look_at(Vec3(0, 0, 5), Vec3(0, 0, 0), Vec3(0, 1, 0));
        Vec4 p = V * Vec4(0, 0, 0, 1);
        // 原点在相机前方 -Z 方向距离 5
        if (!near(p.z, -5.0, 1e-6)) fail++;
    }

    // --- 四元数：绕 Z 轴 90 度旋转 (1,0,0) ---
    {
        Quat q = Quat::from_axis_angle(Vec3(0, 0, 1), deg2rad(90.0));
        Vec3 r = q * Vec3(1, 0, 0);
        if (!near(r.x, 0, 1e-6) || !near(r.y, 1.0, 1e-6)) fail++;
        Quat qi = q.inverse();
        Vec3 back = qi * r;
        if (!near(back.x, 1.0, 1e-6) || !near(back.y, 0, 1e-6)) fail++;
    }

    // --- 射线-球体 ---
    {
        Ray ray(Vec3(0, 0, -5), Vec3(0, 0, 1));
        Sphere s(Vec3(0, 0, 0), 1.0);
        double t = ray_sphere(ray, s);
        if (!near(t, 4.0, 1e-6)) fail++;   // -5 + t = -1 => t=4
    }

    // --- 射线-平面 ---
    {
        Ray ray(Vec3(0, 0, -5), Vec3(0, 0, 1));
        Plane p = Plane::from_points(Vec3(-1,0,0), Vec3(1,0,0), Vec3(0,1,0));
        double t = ray_plane(ray, p);
        if (!near(t, 5.0, 1e-6)) fail++;
    }

    // --- 射线-三角形 ---
    {
        Ray ray(Vec3(0, 0, -1), Vec3(0, 0, 1));
        double u, v;
        double t = ray_triangle(ray,
                                Vec3(-1, -1, 0), Vec3(1, -1, 0), Vec3(0, 1, 0), u, v);
        if (t < 0 || !near(t, 1.0, 1e-6)) fail++;
    }

    // --- 射线-AABB ---
    {
        Ray ray(Vec3(0, 0, -5), Vec3(0, 0, 1));
        AABB b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
        double t = ray_aabb(ray, b);
        if (!near(t, 4.0, 1e-6)) fail++;   // 进入 z=-1 面
    }

    // --- Mat4 逆：变换 * 逆变换 = 单位 ---
    {
        Mat4 M = mat4_translate(3, 4, 5) * mat4_rotate_y(0.7);
        Mat4 Mi = inverse(M);
        Mat4 I = M * Mi;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if (!near(I.m[i][j], i == j ? 1.0 : 0.0, 1e-5)) fail++;
    }

    // --- 新增：AABB-AABB ---
    {
        AABB a(Vec3(0,0,0), Vec3(1,1,1));
        AABB b(Vec3(0.5,0.5,0.5), Vec3(2,2,2));
        AABB c(Vec3(2,2,2), Vec3(3,3,3));
        if (!aabb_intersects(a, b)) fail++;
        if (aabb_intersects(a, c)) fail++;
    }

    // --- 球-球 ---
    {
        Sphere a(Vec3(0,0,0), 1), b(Vec3(0,0,1.5), 1);
        Sphere c(Vec3(0,0,5), 1);
        if (!sphere_intersects(a, b)) fail++;
        if (sphere_intersects(a, c)) fail++;
    }

    // --- 四元数 <-> 矩阵往返 ---
    {
        Mat3 R = mat3_rotate_axis(Vec3(0.3, 0.7, 0.5).normalized(), 1.2);
        Quat q = quat_from_mat3(R);
        Vec3 v(1, 2, 3);
        Vec3 r1 = R * v;
        Vec3 r2 = q * v;
        if ((r1 - r2).length() > 1e-4) fail++;
    }

    // SLERP：两端应等于自身
    {
        Quat a = Quat(1, 0, 0, 0);
        Quat b = Quat(0, 1, 0, 0);
        Quat mid = quat_slerp(a, b, 0.0);
        if (fabs(mid.w - 1.0) > 0.01) fail++;
    }
    // 矩阵求逆：M * inv(M) = I
    {
        Mat4 M = mat4_translate(1, 2, 3) * mat4_rotate_y(0.5);
        Mat4 Prod = M * mat4_inverse(M);
        for (int i = 0; i < 4; i++)
            if (fabs(Prod.m[i][i] - 1.0) > 1e-4) fail++;
    }
    // Catmull-Rom：t=0 应在 p1
    {
        Vec3 p0(0,0,0), p1(1,0,0), p2(2,0,0), p3(3,0,0);
        Vec3 r = catmull_rom(p0, p1, p2, p3, 0.0);
        if (fabs(r.x - 1.0) > 0.01) fail++;
    }
    return fail;
}

} // namespace gfx3d
} // namespace nefu
