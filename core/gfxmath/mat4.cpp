// nefuOS gfxmath —— 4x4 矩阵实现 + 自测
#include "gfxmath/mat4.h"
#include <cstdio>

namespace nefu {
namespace gfx {

Mat4 Mat4::rotation_x(double a) {
    Mat4 r = identity();
    double c = std::cos(a), s = std::sin(a);
    r.m[1][1] = c;  r.m[1][2] = -s;
    r.m[2][1] = s;  r.m[2][2] = c;
    return r;
}

Mat4 Mat4::rotation_y(double a) {
    Mat4 r = identity();
    double c = std::cos(a), s = std::sin(a);
    r.m[0][0] = c;  r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

Mat4 Mat4::rotation_z(double a) {
    Mat4 r = identity();
    double c = std::cos(a), s = std::sin(a);
    r.m[0][0] = c;  r.m[0][1] = -s;
    r.m[1][0] = s;  r.m[1][1] = c;
    return r;
}

Mat4 Mat4::operator*(const Mat4& b) const {
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            double s = 0;
            for (int k = 0; k < 4; k++) s += m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    return r;
}

Vec3 Mat4::transform(const Vec3& v) const {
    double x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
    double y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
    double z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
    return Vec3(x, y, z);
}

Vec3 Mat4::transform_dir(const Vec3& v) const {
    double x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z;
    double y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z;
    double z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z;
    return Vec3(x, y, z);
}

Mat4 Mat4::transposed() const {
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r.m[i][j] = m[j][i];
    return r;
}

Mat4 Mat4::inverse() const {
    // 高斯-约当消元求增广矩阵逆
    double a[4][8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) a[i][j] = m[i][j];
        for (int j = 0; j < 4; j++) a[i][4 + j] = (i == j) ? 1 : 0;
    }
    for (int col = 0; col < 4; col++) {
        // 选主元
        int best = col;
        for (int r = col + 1; r < 4; r++)
            if (std::abs(a[r][col]) > std::abs(a[best][col])) best = r;
        if (std::abs(a[best][col]) < 1e-12) return identity();   // 奇异
        for (int j = 0; j < 8; j++) { double t = a[col][j]; a[col][j] = a[best][j]; a[best][j] = t; }
        double piv = a[col][col];
        for (int j = 0; j < 8; j++) a[col][j] /= piv;
        for (int r = 0; r < 4; r++) {
            if (r == col) continue;
            double f = a[r][col];
            for (int j = 0; j < 8; j++) a[r][j] -= f * a[col][j];
        }
    }
    Mat4 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) r.m[i][j] = a[i][4 + j];
    return r;
}

Mat4 Mat4::perspective(double fovy, double aspect, double znear, double zfar) {
    Mat4 p;
    double f = 1.0 / std::tan(fovy * 0.5);
    p.m[0][0] = f / aspect;
    p.m[1][1] = f;
    p.m[2][2] = (zfar + znear) / (znear - zfar);
    p.m[2][3] = (2 * zfar * znear) / (znear - zfar);
    p.m[3][2] = -1;
    return p;
}

Mat4 Mat4::ortho(double l, double r, double b, double t, double n, double f) {
    Mat4 o = identity();
    o.m[0][0] = 2 / (r - l);
    o.m[1][1] = 2 / (t - b);
    o.m[2][2] = -2 / (f - n);
    o.m[0][3] = -(r + l) / (r - l);
    o.m[1][3] = -(t + b) / (t - b);
    o.m[2][3] = -(f + n) / (f - n);
    return o;
}

// ---- self test ----
int Mat4::self_test() {
    int fails = 0;
    // 1. 单位矩阵
    {
        Mat4 I = Mat4::identity();
        Vec3 v(2, 3, 4);
        Vec3 t = I.transform(v);
        if (t.x != 2 || t.y != 3 || t.z != 4) fails++;
        Mat4 p = I * I;
        if (p.m[0][0] != 1 || p.m[3][3] != 1) fails++;
    }
    // 2. 平移
    {
        Mat4 T = Mat4::translation(10, 20, 30);
        Vec3 v = T.transform(Vec3(1, 2, 3));
        if (v.x != 11 || v.y != 22 || v.z != 33) fails++;
        // 平移不影响方向
        Vec3 d = T.transform_dir(Vec3(1, 0, 0));
        if (d.x != 1 || d.y != 0 || d.z != 0) fails++;
    }
    // 3. 缩放 + 平移组合：先缩放再平移
    {
        Mat4 S = Mat4::scaling(2, 3, 4);
        Mat4 T = Mat4::translation(1, 1, 1);
        Mat4 M = T * S;   // 先 S 后 T
        Vec3 v = M.transform(Vec3(1, 1, 1));
        if (v.x != 3 || v.y != 4 || v.z != 5) fails++;
    }
    // 4. 旋转 Z 轴 90 度
    {
        Mat4 R = Mat4::rotation_z(3.14159265358979 / 2);
        Vec3 v = R.transform(Vec3(1, 0, 0));
        if (std::abs(v.x) > 1e-9 || std::abs(v.y - 1) > 1e-9) fails++;
    }
    // 5. 求逆：M * M^-1 = I
    {
        Mat4 T = Mat4::translation(5, -3, 2);
        Mat4 S = Mat4::scaling(2, 0.5, 4);
        Mat4 M = T * S;
        Mat4 Mi = M.inverse();
        Mat4 R = M * Mi;
        bool id = true;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                double want = (i == j) ? 1.0 : 0.0;
                if (std::abs(R.m[i][j] - want) > 1e-9) id = false;
            }
        if (!id) fails++;
    }
    // 6. 投影矩阵基本性质
    {
        Mat4 P = Mat4::perspective(1.0, 1.5, 0.1, 100.0);
        // 原点经透视后 w 应为 0（z=-1 行）
        double w = P.m[3][0] * 0 + P.m[3][1] * 0 + P.m[3][2] * (-1) + P.m[3][3] * 1;
        if (std::abs(w - 1.0) > 1e-9) fails++;   // 透视除法分母 w = -z
        Mat4 O = Mat4::ortho(-1, 1, -1, 1, 0.1, 100);
        Vec3 c = O.transform(Vec3(0, 0, -50));   // 视锥中点的深度应映射到 0 附近
        if (c.z > 0.5 || c.z < -0.5) fails++;
    }
    return fails;
}

} // namespace gfx
} // namespace nefu
