// nefuOS 数学扩展库 —— 稠密矩阵模块
// 动态行/列的 double 稠密矩阵，行主序存储（m[i*cols+j]）。
// 覆盖：四则运算、转置、行列式(LU)、逆(Gauss-Jordan)、秩、迹、
//       LU / QR(Gram-Schmidt) / Cholesky 分解、线性方程组求解、
//       特征值/特征向量（幂迭代 / 逆幂迭代）。
// 内存：内部用 new[]/delete[]（路由到 kalloc/kfree），深拷贝语义。
#pragma once

namespace nefu {
namespace mathext {

// ---------------- 稠密矩阵类 ----------------
// 约定：空矩阵(rows=0 或 m=null)不参与运算；所有函数对尺寸不匹配返回空矩阵。
class Matrix {
public:
    int    rows;   // 行数
    int    cols;   // 列数
    double* m;     // rows*cols 个元素，行主序

    Matrix();
    Matrix(int r, int c);                       // 未初始化（调用方负责）
    Matrix(int r, int c, const double* flat);   // 用 row-major 扁平数组初始化
    Matrix(const Matrix& o);                    // 深拷贝
    ~Matrix();
    Matrix& operator=(const Matrix& o);

    double& operator()(int i, int j)       { return m[i * cols + j]; }
    double  operator()(int i, int j) const { return m[i * cols + j]; }

    int nrows() const { return rows; }
    int ncols() const { return cols; }
    bool valid() const { return rows > 0 && cols > 0 && m != 0; }
    void set_all(double v);

    // n 阶单位矩阵
    static Matrix identity(int n);
    // 打印到调用方缓冲区（不带 %f，由调用方格式化），这里只返回元素指针
    double* data() { return m; }
};

// ---------------- 基本运算 ----------------
Matrix mat_add(const Matrix& A, const Matrix& B);      // A+B
Matrix mat_sub(const Matrix& A, const Matrix& B);      // A-B
Matrix mat_mul(const Matrix& A, const Matrix& B);       // A*B（标准矩阵乘）
Matrix mat_scale(const Matrix& A, double s);            // s*A
Matrix mat_transpose(const Matrix& A);                  // A^T

// ---------------- 行列式 / 逆 / 秩 / 迹 ----------------
// 行列式：LU 分解 + 部分选主元，det = (-1)^pivot_swaps * prod(diag U)
double mat_determinant(const Matrix& A);
// 逆矩阵：Gauss-Jordan，增广矩阵 [A|I] 消成 [I|A^-1]；奇异返回空矩阵
Matrix mat_inverse(const Matrix& A);
// 秩：行梯形化简后非零行数
int    mat_rank(const Matrix& A);
// 迹：仅方阵，主对角线元素之和
double mat_trace(const Matrix& A);

// ---------------- 解线性方程组 ----------------
// 解 A x = b（b 为长度 n 的列向量）。返回长度 n 的解向量；失败返回空指针数组。
// 实现：带部分选主元的高斯消元。
double* mat_solve(const Matrix& A, const double* b);

// ---------------- 矩阵分解 ----------------
// LU 分解（Doolittle：L 单位下三角，U 上三角）。返回是否成功。
// L、U 为出参（调用方持有所有权）。
bool lu_decompose(const Matrix& A, Matrix& L, Matrix& U);
// QR 分解（修正 Gram-Schmidt）：A = Q R，Q 列正交，R 上三角。
bool qr_decompose(const Matrix& A, Matrix& Q, Matrix& R);
// Cholesky 分解（仅对称正定矩阵）：A = L L^T，L 为下三角。
bool cholesky_decompose(const Matrix& A, Matrix& L);

// ---------------- 特征值 / 特征向量 ----------------
// 幂迭代：求模最大的特征值。out_vec 为出参（长度 n 的向量，调用方持有）。
// 返回收敛到的特征值；不收敛时返回最后一次估计。
double eigen_power(const Matrix& A, double* out_vec, int max_iter = 2000, double eps = 1e-12);
// 逆幂迭代：求模最小的特征值（用 A 解线性方程组代替直接求逆）。
double eigen_inv_power(const Matrix& A, double* out_vec, int max_iter = 2000, double eps = 1e-12);

// ---------------- 范数 / 条件数 ----------------
double mat_norm_frobenius(const Matrix& A);   // Frobenius 范数 sqrt(sum aij^2)
double mat_norm_inf(const Matrix& A);          // 无穷范数（最大行和）
double mat_cond_number(const Matrix& A);       // 无穷范数条件数 ||A||*||A^-1||，奇异返回 -1

// ---------------- 对称判定 / 矩阵幂 ----------------
bool   mat_is_symmetric(const Matrix& A, double eps = 1e-9);
Matrix mat_pow(const Matrix& A, int k);       // A^k（快速幂，仅方阵）

// ---------------- 对称矩阵 Jacobi 特征值 ----------------
// 对实对称矩阵用 Jacobi 旋转求全部特征值。特征值写入 out_eig（长度 n）。
// 返回是否成功；特征向量矩阵可选出参 out_V（列向量）。
bool jacobi_eigen(const Matrix& A, double* out_eig, Matrix* out_V = 0,
                  int max_iter = 200, double eps = 1e-12);
// ---------------- 向量运算 ----------------
double vec_dot(const double* a, const double* b, int n);       // 点积
double vec_norm(const double* a, int n);                        // 2-范数
void   vec_normalize(double* a, int n);                         // 原地归一化
double vec_cross2d(const double* a, const double* b);          // 二维叉积标量

// ---------------- 最小二乘 / 伪逆 ----------------
// 解超定方程 min ||Ax-b||^2：返回长度 n 的解。
double* lstsq_solve(const Matrix& A, const double* b);
// Moore-Penrose 伪逆：A^+ = (A^T A)^-1 A^T（方阵情形）。
Matrix  mat_pinv(const Matrix& A);
// ---------------- 自检 ----------------
// 已知值：
//   det([[1,2],[3,4]]) = -2
//   inv([[1,2],[3,4]]) = [[-2,1],[1.5,-0.5]]
//   A*A^-1 ≈ I
//   solve [[2,0],[0,1]] x = [4,3] -> [2,3]
//   幂迭代 [[2,0],[0,1]] 主特征值 = 2
// 返回失败条数。
int matrix_self_test();

} // namespace mathext
} // namespace nefu
