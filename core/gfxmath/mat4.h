// nefuOS gfxmath —— 4x4 矩阵 mat4
// 教学版：列主序 4x4 变换矩阵（平移/缩放/旋转/投影）及其乘法、求逆。
// 图形学中把模型、视图、投影变换统一为矩阵乘法。class / STL / 中文注释。
#pragma once
#include <cmath>
#include "gfxmath/vec3.h"

namespace nefu {
namespace gfx {

// 4x4 矩阵（按行存储 m[r][c]）
struct Mat4 {
    double m[4][4];

    Mat4() {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++) m[r][c] = 0;
    }

    // 单位矩阵
    static Mat4 identity() {
        Mat4 a;
        for (int i = 0; i < 4; i++) a.m[i][i] = 1;
        return a;
    }

    // 平移矩阵
    static Mat4 translation(double tx, double ty, double tz) {
        Mat4 a = identity();
        a.m[0][3] = tx;
        a.m[1][3] = ty;
        a.m[2][3] = tz;
        return a;
    }

    // 缩放矩阵
    static Mat4 scaling(double sx, double sy, double sz) {
        Mat4 a = identity();
        a.m[0][0] = sx;
        a.m[1][1] = sy;
        a.m[2][2] = sz;
        return a;
    }

    // 绕 X / Y / Z 轴旋转矩阵（弧度）
    static Mat4 rotation_x(double a);
    static Mat4 rotation_y(double a);
    static Mat4 rotation_z(double a);

    // 乘法：this * b
    Mat4 operator*(const Mat4& b) const;
    // 变换向量（视为列向量，w=1）
    Vec3 transform(const Vec3& v) const;
    // 变换方向（w=0，忽略平移）
    Vec3 transform_dir(const Vec3& v) const;
    // 转置
    Mat4 transposed() const;
    // 求逆（高斯消元；不可逆返回单位阵）
    Mat4 inverse() const;

    // 透视投影矩阵（fovy 垂直视角弧度，aspect 宽高比，znear/zfar 近远裁剪面）
    static Mat4 perspective(double fovy, double aspect, double znear, double zfar);
    // 正交投影矩阵
    static Mat4 ortho(double l, double r, double b, double t, double n, double f);

    // ---- self test ----
    static int self_test();
};

} // namespace gfx
} // namespace nefu
