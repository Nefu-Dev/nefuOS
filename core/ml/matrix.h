// nefuOS 机器学习库 —— 定点稠密矩阵（Q16.16）
// 行主序存储 m[i*cols+j]，元素类型 nefu::fx::fix（int32，值 = 原始值*65536）。
// 覆盖：四则 / 转置 / 行列式(LU 部分选主元) / 求逆(Gauss-Jordan) / 解线性方程组 /
//       迹 / 范数 / 对称矩阵 Jacobi 特征值分解。
// 内存：new[]/delete[]（路由到 kalloc/kfree），深拷贝语义。
// 注意：所有"浮点"都用 fx 整数运算，宿主测试与裸机运行结果一致。
#pragma once
#include <stdint.h>
#include "../lib/softmath.h"
#include "ml_util.h"

namespace nefu {
namespace ml {

// ---------------- 定点矩阵类 ----------------
class FxMat {
public:
    int   rows;
    int   cols;
    fix*  m;          // rows*cols 个元素，行主序

    FxMat();
    FxMat(int r, int c);                       // 未初始化
    FxMat(int r, int c, const fix* flat);     // 用 row-major 扁平数组初始化
    FxMat(const FxMat& o);                     // 深拷贝
    ~FxMat();
    FxMat& operator=(const FxMat& o);

    fix& operator()(int i, int j)       { return m[i * cols + j]; }
    fix  operator()(int i, int j) const { return m[i * cols + j]; }

    bool valid() const { return rows > 0 && cols > 0 && m != 0; }
    void set_all(fix v);
    static FxMat identity(int n);
    // 用浮点字面量数组初始化的便捷构造（仅 self_test 用），
    // fv 元素为 double，内部转 fx。宿主测试编译允许 include <cmath>。
    static FxMat from_doubles(int r, int c, const double* fv);
};

// ---------------- 基本运算 ----------------
FxMat mat_add(const FxMat& A, const FxMat& B);   // A+B
FxMat mat_sub(const FxMat& A, const FxMat& B);   // A-B
FxMat mat_mul(const FxMat& A, const FxMat& B);    // A*B
FxMat mat_scale(const FxMat& A, fix s);            // s*A
FxMat mat_transpose(const FxMat& A);              // A^T

// ---------------- 行列式 / 逆 / 迹 ----------------
fix  mat_determinant(const FxMat& A);             // LU + 部分选主元
FxMat mat_inverse(const FxMat& A);                // Gauss-Jordan，奇异返回空矩阵
fix  mat_trace(const FxMat& A);                   // 主对角线之和

// ---------------- 解线性方程组 A x = b ----------------
// b 长度 n；返回长度 n 的解（调用方 delete[]）；失败返回 0。
fix* mat_solve(const FxMat& A, const fix* b);

// ---------------- 对称矩阵 Jacobi 特征值 ----------------
// 对实对称矩阵反复旋转，收敛后对角元即特征值（写入 out_eig，长度 n）。
// out_V 可选出参：列向量为特征向量矩阵（调用方持有）。返回是否成功。
bool jacobi_eigen(const FxMat& A, fix* out_eig, FxMat* out_V = 0,
                  int max_iter = 100, fix tol = 64);  // tol ≈ 0.001

// ---------------- 范数 ----------------
fix mat_norm_frobenius(const FxMat& A);            // sqrt(Σ aij^2)

// ---------------- 矩阵分解 ----------------
// LU 分解（Doolittle：L 单位下三角，U 上三角）。成功返回 true。
bool lu_decompose(const FxMat& A, FxMat& L, FxMat& U);
// QR 分解（修正 Gram-Schmidt）：A = Q R，Q 列正交，R 上三角。
bool qr_decompose(const FxMat& A, FxMat& Q, FxMat& R);
// Cholesky 分解（对称正定 A = L L^T，L 下三角）。成功返回 true。
bool cholesky_decompose(const FxMat& A, FxMat& L);

// ---------------- 秩 / 条件数 / 矩阵幂 ----------------
int   mat_rank(const FxMat& A);                       // 行梯形化简后非零行数
fix   mat_cond_number(const FxMat& A);               // 无穷范数条件数，奇异返回 -1
FxMat mat_pow(const FxMat& A, int k);                // A^k（快速幂，仅方阵）

// ---------------- 自检 ----------------
// 已知值（Q16.16 下容差约 0.02）：
//   det([[1,2],[3,4]]) = -2
//   inv([[1,2],[3,4]]) = [[-2,1],[1.5,-0.5]]
//   A*A^-1 ≈ I
//   solve diag(2,1) x = [4,3] -> [2,3]
// 返回失败条数。
int matrix_self_test();

} // namespace ml
} // namespace nefu
