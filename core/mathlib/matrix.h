// nefuOS mathlib —— 矩阵 matrix
// 教学版：定长 8x8 双精度矩阵，支持加/减/乘/转置/行列式/逆。
// 行列式用递归展开（教学演示，小于 8 阶可用）。
#pragma once
#include <cstring>

namespace nefu {
namespace mathx {

// 8x8 定长矩阵（实际阶数 rows x cols，≤8）
struct Matrix {
    static const int N = 8;
    int rows, cols;
    double m[N][N];

    Matrix();                                       // 1x1 零矩阵
    Matrix(int r, int c);                           // rxc 零矩阵
    static Matrix identity(int n);                  // n 阶单位阵

    double& at(int r, int c) { return m[r][c]; }
    double  at(int r, int c) const { return m[r][c]; }

    Matrix& add(const Matrix& b);                   // 同型加法
    Matrix& sub(const Matrix& b);
    Matrix  mul(const Matrix& b) const;             // 矩阵乘法
    Matrix  transposed() const;                     // 转置
    double  det() const;                            // 行列式（递归）
    Matrix  inverse() const;                        // 逆矩阵（伴随法，教学版）
    Matrix  scaled(double k) const;

    void fill(double v);                            // 全部填 v
    bool near(const Matrix& b, double eps = 1e-9) const;
};

int matrix_self_test();

} // namespace mathx
} // namespace nefu
