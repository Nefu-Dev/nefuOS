#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 数学扩展库 —— 稠密矩阵模块实现
#include "matrix.h"
#include "complex.h"   // 复用 d_close 近似判据
#include <cmath>
#include <cstring>
#include <cstdio>

namespace nefu {
namespace mathext {

// ---------------- 生命周期 ----------------
Matrix::Matrix() : rows(0), cols(0), m(0) {}

Matrix::Matrix(int r, int c) : rows(r), cols(c), m(0) {
    if (r > 0 && c > 0) m = new double[(size_t)(r * c)];
}

Matrix::Matrix(int r, int c, const double* flat) : rows(r), cols(c), m(0) {
    if (r > 0 && c > 0) {
        m = new double[(size_t)(r * c)];
        for (int i = 0; i < r * c; i++) m[i] = flat[i];
    }
}

Matrix::Matrix(const Matrix& o) : rows(o.rows), cols(o.cols), m(0) {
    if (o.valid()) {
        m = new double[(size_t)(rows * cols)];
        for (int i = 0; i < rows * cols; i++) m[i] = o.m[i];
    }
}

Matrix::~Matrix() {
    if (m) delete[] m;
    m = 0; rows = 0; cols = 0;
}

Matrix& Matrix::operator=(const Matrix& o) {
    if (this != &o) {
        if (m) delete[] m;
        rows = o.rows; cols = o.cols; m = 0;
        if (o.valid()) {
            m = new double[(size_t)(rows * cols)];
            for (int i = 0; i < rows * cols; i++) m[i] = o.m[i];
        }
    }
    return *this;
}

void Matrix::set_all(double v) {
    if (!valid()) return;
    for (int i = 0; i < rows * cols; i++) m[i] = v;
}

Matrix Matrix::identity(int n) {
    Matrix R(n, n);
    R.set_all(0.0);
    for (int i = 0; i < n; i++) R(i, i) = 1.0;
    return R;
}

// ---------------- 基本运算 ----------------
Matrix mat_add(const Matrix& A, const Matrix& B) {
    if (A.rows != B.rows || A.cols != B.cols) return Matrix();
    Matrix R(A.rows, A.cols);
    for (int i = 0; i < A.rows * A.cols; i++) R.m[i] = A.m[i] + B.m[i];
    return R;
}

Matrix mat_sub(const Matrix& A, const Matrix& B) {
    if (A.rows != B.rows || A.cols != B.cols) return Matrix();
    Matrix R(A.rows, A.cols);
    for (int i = 0; i < A.rows * A.cols; i++) R.m[i] = A.m[i] - B.m[i];
    return R;
}

Matrix mat_mul(const Matrix& A, const Matrix& B) {
    if (A.cols != B.rows) return Matrix();
    Matrix R(A.rows, B.cols);
    R.set_all(0.0);   // 必须清零：下面是 += 累乘
    // 三层循环 i-k-j，对行主序缓存友好
    for (int i = 0; i < A.rows; i++) {
        for (int k = 0; k < A.cols; k++) {
            double aik = A(i, k);
            for (int j = 0; j < B.cols; j++) {
                R(i, j) += aik * B(k, j);
            }
        }
    }
    return R;
}

Matrix mat_scale(const Matrix& A, double s) {
    Matrix R(A.rows, A.cols);
    for (int i = 0; i < A.rows * A.cols; i++) R.m[i] = A.m[i] * s;
    return R;
}

Matrix mat_transpose(const Matrix& A) {
    Matrix R(A.cols, A.rows);
    for (int i = 0; i < A.rows; i++)
        for (int j = 0; j < A.cols; j++)
            R(j, i) = A(i, j);
    return R;
}

// ---------------- 行列式（LU + 部分选主元） ----------------
double mat_determinant(const Matrix& A) {
    if (A.rows != A.cols || !A.valid()) return 0.0;
    int n = A.rows;
    // 工作拷贝
    double* M = new double[(size_t)(n * n)];
    for (int i = 0; i < n * n; i++) M[i] = A.m[i];

    double det = 1.0;
    for (int col = 0; col < n; col++) {
        // 部分选主元：找该列绝对值最大的行
        int pivot = col;
        double best = std::fabs(M[col * n + col]);
        for (int r = col + 1; r < n; r++) {
            double v = std::fabs(M[r * n + col]);
            if (v > best) { best = v; pivot = r; }
        }
        if (best < 1e-300) { delete[] M; return 0.0; }   // 奇异
        if (pivot != col) {
            for (int j = 0; j < n; j++) {
                double t = M[col * n + j];
                M[col * n + j] = M[pivot * n + j];
                M[pivot * n + j] = t;
            }
            det = -det;   // 每次换行行列式变号
        }
        double piv = M[col * n + col];
        det *= piv;
        // 消去下方
        for (int r = col + 1; r < n; r++) {
            double f = M[r * n + col] / piv;
            for (int j = col; j < n; j++) M[r * n + j] -= f * M[col * n + j];
        }
    }
    delete[] M;
    return det;
}

// ---------------- 逆矩阵（Gauss-Jordan） ----------------
Matrix mat_inverse(const Matrix& A) {
    if (A.rows != A.cols || !A.valid()) return Matrix();
    int n = A.rows;
    // 增广矩阵 [A | I]，n x 2n
    double* M = new double[(size_t)(n * 2 * n)];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            M[i * 2 * n + j] = A(i, j);
            M[i * 2 * n + n + j] = (i == j) ? 1.0 : 0.0;
        }

    for (int col = 0; col < n; col++) {
        // 选主元
        int pivot = col;
        double best = std::fabs(M[col * 2 * n + col]);
        for (int r = col + 1; r < n; r++) {
            double v = std::fabs(M[r * 2 * n + col]);
            if (v > best) { best = v; pivot = r; }
        }
        if (best < 1e-300) { delete[] M; return Matrix(); }   // 奇异
        if (pivot != col) {
            for (int j = 0; j < 2 * n; j++) {
                double t = M[col * 2 * n + j];
                M[col * 2 * n + j] = M[pivot * 2 * n + j];
                M[pivot * 2 * n + j] = t;
            }
        }
        double piv = M[col * 2 * n + col];
        // 整行除以主元，使主元位置变 1
        for (int j = 0; j < 2 * n; j++) M[col * 2 * n + j] /= piv;
        // 消去其它所有行（Gauss-Jordan 的关键：上三角外也消）
        for (int r = 0; r < n; r++) {
            if (r == col) continue;
            double f = M[r * 2 * n + col];
            for (int j = 0; j < 2 * n; j++) M[r * 2 * n + j] -= f * M[col * 2 * n + j];
        }
    }

    Matrix Inv(n, n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            Inv(i, j) = M[i * 2 * n + n + j];
    delete[] M;
    return Inv;
}

// ---------------- 秩（行梯形） ----------------
int mat_rank(const Matrix& A) {
    if (!A.valid()) return 0;
    int n = A.rows, m = A.cols;
    double* M = new double[(size_t)(n * m)];
    for (int i = 0; i < n * m; i++) M[i] = A.m[i];

    int rank = 0;
    for (int col = 0; col < m && rank < n; col++) {
        // 找主元
        int pivot = -1;
        double best = 1e-300;
        for (int r = rank; r < n; r++) {
            double v = std::fabs(M[r * m + col]);
            if (v > best) { best = v; pivot = r; }
        }
        if (pivot < 0) continue;   // 该列全零，下一列
        if (pivot != rank) {
            for (int j = 0; j < m; j++) {
                double t = M[rank * m + j];
                M[rank * m + j] = M[pivot * m + j];
                M[pivot * m + j] = t;
            }
        }
        double piv = M[rank * m + col];
        for (int r = 0; r < n; r++) {
            if (r == rank) continue;
            double f = M[r * m + col] / piv;
            for (int j = col; j < m; j++) M[r * m + j] -= f * M[rank * m + j];
        }
        rank++;
    }
    delete[] M;
    return rank;
}

// ---------------- 迹 ----------------
double mat_trace(const Matrix& A) {
    if (A.rows != A.cols || !A.valid()) return 0.0;
    double tr = 0.0;
    for (int i = 0; i < A.rows; i++) tr += A(i, i);
    return tr;
}

// ---------------- 解 Ax = b（高斯消元 + 部分选主元） ----------------
double* mat_solve(const Matrix& A, const double* b) {
    if (A.rows != A.cols || !A.valid()) return 0;
    int n = A.rows;
    // 构造 n x (n+1) 增广矩阵
    double* M = new double[(size_t)(n * (n + 1))];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) M[i * (n + 1) + j] = A(i, j);
        M[i * (n + 1) + n] = b[i];
    }
    // 前向消元成上三角
    for (int col = 0; col < n; col++) {
        int pivot = col;
        double best = std::fabs(M[col * (n + 1) + col]);
        for (int r = col + 1; r < n; r++) {
            double v = std::fabs(M[r * (n + 1) + col]);
            if (v > best) { best = v; pivot = r; }
        }
        if (best < 1e-300) { delete[] M; return 0; }
        if (pivot != col) {
            for (int j = 0; j <= n; j++) {
                double t = M[col * (n + 1) + j];
                M[col * (n + 1) + j] = M[pivot * (n + 1) + j];
                M[pivot * (n + 1) + j] = t;
            }
        }
        double piv = M[col * (n + 1) + col];
        for (int r = col + 1; r < n; r++) {
            double f = M[r * (n + 1) + col] / piv;
            for (int j = col; j <= n; j++) M[r * (n + 1) + j] -= f * M[col * (n + 1) + j];
        }
    }
    // 回代
    double* x = new double[(size_t)n];
    for (int i = n - 1; i >= 0; i--) {
        double s = M[i * (n + 1) + n];
        for (int j = i + 1; j < n; j++) s -= M[i * (n + 1) + j] * x[j];
        x[i] = s / M[i * (n + 1) + i];
    }
    delete[] M;
    return x;
}

// ---------------- LU 分解（Doolittle） ----------------
bool lu_decompose(const Matrix& A, Matrix& L, Matrix& U) {
    if (A.rows != A.cols || !A.valid()) return false;
    int n = A.rows;
    L = Matrix::identity(n);
    U = Matrix(n, n);
    for (int i = 0; i < n; i++) {
        // U 的第 i 行
        for (int j = i; j < n; j++) {
            double s = 0.0;
            for (int k = 0; k < i; k++) s += L(i, k) * U(k, j);
            U(i, j) = A(i, j) - s;
        }
        // L 的第 i 列（对角元恒为 1）
        for (int j = i + 1; j < n; j++) {
            double s = 0.0;
            for (int k = 0; k < i; k++) s += L(j, k) * U(k, i);
            if (std::fabs(U(i, i)) < 1e-300) return false;
            L(j, i) = (A(j, i) - s) / U(i, i);
        }
    }
    return true;
}

// ---------------- QR 分解（修正 Gram-Schmidt，数值更稳） ----------------
bool qr_decompose(const Matrix& A, Matrix& Q, Matrix& R) {
    if (!A.valid()) return false;
    int n = A.rows, m = A.cols;
    Q = Matrix(n, m);
    R = Matrix(m, m);
    // 逐列处理：q_j = a_j - sum_{k<j} (q_k . a_j) q_k，再单位化
    for (int j = 0; j < m; j++) {
        // v = A 的第 j 列
        double* v = new double[(size_t)n];
        for (int i = 0; i < n; i++) v[i] = A(i, j);
        for (int k = 0; k < j; k++) {
            double dot = 0.0;
            for (int i = 0; i < n; i++) dot += Q(i, k) * v[i];
            R(k, j) = dot;
            for (int i = 0; i < n; i++) v[i] -= dot * Q(i, k);
        }
        double norm = 0.0;
        for (int i = 0; i < n; i++) norm += v[i] * v[i];
        norm = std::sqrt(norm);
        if (norm < 1e-300) { delete[] v; return false; }   // 列线性相关
        R(j, j) = norm;
        for (int i = 0; i < n; i++) Q(i, j) = v[i] / norm;
        delete[] v;
    }
    return true;
}

// ---------------- Cholesky 分解（对称正定） ----------------
bool cholesky_decompose(const Matrix& A, Matrix& L) {
    if (A.rows != A.cols || !A.valid()) return false;
    int n = A.rows;
    L = Matrix(n, n);
    L.set_all(0.0);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double s = 0.0;
            for (int k = 0; k < j; k++) s += L(i, k) * L(j, k);
            if (i == j) {
                double d = A(i, i) - s;
                if (d < 1e-300) return false;   // 非正定
                L(i, j) = std::sqrt(d);
            } else {
                L(i, j) = (A(i, j) - s) / L(j, j);
            }
        }
    }
    return true;
}

// ---------------- 幂迭代（模最大特征值） ----------------
double eigen_power(const Matrix& A, double* out_vec, int max_iter, double eps) {
    if (A.rows != A.cols || !A.valid()) return 0.0;
    int n = A.rows;
    // 初始向量：全 1 归一化
    double* v = new double[(size_t)n];
    double norm = 0.0;
    for (int i = 0; i < n; i++) { v[i] = 1.0; norm += 1.0; }
    norm = std::sqrt(norm);
    for (int i = 0; i < n; i++) v[i] /= norm;

    double eig = 0.0;
    for (int it = 0; it < max_iter; it++) {
        // w = A v
        double* w = new double[(size_t)n];
        for (int i = 0; i < n; i++) {
            double s = 0.0;
            for (int j = 0; j < n; j++) s += A(i, j) * v[j];
            w[i] = s;
        }
        // Rayleigh 商 v^T A v / v^T v 即特征值估计
        double num = 0.0, den = 0.0;
        for (int i = 0; i < n; i++) { num += v[i] * w[i]; den += v[i] * v[i]; }
        double eig_new = (den > 0) ? num / den : 0.0;
        // 归一化 w -> v
        double wn = 0.0;
        for (int i = 0; i < n; i++) wn += w[i] * w[i];
        wn = std::sqrt(wn);
        if (wn < 1e-300) { delete[] w; break; }
        for (int i = 0; i < n; i++) v[i] = w[i] / wn;
        delete[] w;
        if (std::fabs(eig_new - eig) < eps) { eig = eig_new; break; }
        eig = eig_new;
    }
    for (int i = 0; i < n; i++) out_vec[i] = v[i];
    delete[] v;
    return eig;
}

// ---------------- 逆幂迭代（模最小特征值） ----------------
double eigen_inv_power(const Matrix& A, double* out_vec, int max_iter, double eps) {
    if (A.rows != A.cols || !A.valid()) return 0.0;
    int n = A.rows;
    double* v = new double[(size_t)n];
    double norm = 0.0;
    for (int i = 0; i < n; i++) { v[i] = 1.0; norm += 1.0; }
    norm = std::sqrt(norm);
    for (int i = 0; i < n; i++) v[i] /= norm;

    double eig = 0.0;
    for (int it = 0; it < max_iter; it++) {
        // 解 A w = v（等价于 w = A^-1 v）
        double* w = mat_solve(A, v);
        if (!w) { delete[] v; return 0.0; }
        double wn = 0.0;
        for (int i = 0; i < n; i++) wn += w[i] * w[i];
        wn = std::sqrt(wn);
        if (wn < 1e-300) { delete[] w; delete[] v; return 0.0; }
        for (int i = 0; i < n; i++) w[i] /= wn;
        // Rayleigh 商（对 A^-1）的倒数即原矩阵特征值
        double num = 0.0, den = 0.0;
        for (int i = 0; i < n; i++) { num += v[i] * w[i]; den += v[i] * v[i]; }
        double inv_eig = (den > 0) ? num / den : 0.0;
        double eig_new = (std::fabs(inv_eig) > 1e-300) ? 1.0 / inv_eig : 0.0;
        for (int i = 0; i < n; i++) v[i] = w[i];
        delete[] w;
        if (std::fabs(eig_new - eig) < eps) { eig = eig_new; break; }
        eig = eig_new;
    }
    for (int i = 0; i < n; i++) out_vec[i] = v[i];
    delete[] v;
    return eig;
}

// ---------------- 范数 / 条件数 ----------------
double mat_norm_frobenius(const Matrix& A) {
    if (!A.valid()) return 0.0;
    double s = 0.0;
    for (int i = 0; i < A.rows * A.cols; i++) s += A.m[i] * A.m[i];
    return std::sqrt(s);
}

double mat_norm_inf(const Matrix& A) {
    if (!A.valid()) return 0.0;
    double best = 0.0;
    for (int i = 0; i < A.rows; i++) {
        double s = 0.0;
        for (int j = 0; j < A.cols; j++) s += std::fabs(A(i, j));
        if (s > best) best = s;
    }
    return best;
}

double mat_cond_number(const Matrix& A) {
    Matrix Ai = mat_inverse(A);
    if (!Ai.valid()) return -1.0;
    return mat_norm_inf(A) * mat_norm_inf(Ai);
}

// ---------------- 对称判定 / 矩阵幂 ----------------
bool mat_is_symmetric(const Matrix& A, double eps) {
    if (A.rows != A.cols || !A.valid()) return false;
    for (int i = 0; i < A.rows; i++)
        for (int j = i + 1; j < A.cols; j++)
            if (std::fabs(A(i, j) - A(j, i)) > eps) return false;
    return true;
}

Matrix mat_pow(const Matrix& A, int k) {
    if (A.rows != A.cols || !A.valid()) return Matrix();
    Matrix result = Matrix::identity(A.rows);
    Matrix base = A;
    int exp = k;
    while (exp > 0) {
        if (exp & 1) result = mat_mul(result, base);
        base = mat_mul(base, base);
        exp >>= 1;
    }
    return result;
}

// ---------------- 对称矩阵 Jacobi 特征值 ----------------
bool jacobi_eigen(const Matrix& A, double* out_eig, Matrix* out_V,
                  int max_iter, double eps) {
    if (A.rows != A.cols || !A.valid()) return false;
    int n = A.rows;
    // 拷贝 A
    Matrix M(n, n);
    for (int i = 0; i < n * n; i++) M.m[i] = A.m[i];
    Matrix V = Matrix::identity(n);
    for (int it = 0; it < max_iter; it++) {
        // 找最大非对角元
        int p = 0, q = 1;
        double best = 0.0;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                double v = std::fabs(M(i, j));
                if (v > best) { best = v; p = i; q = j; }
            }
        if (best < eps) break;
        // 旋转角
        double theta = 0.5 * std::atan2(2.0 * M(p, q), M(q, q) - M(p, p));
        double c = std::cos(theta), s = std::sin(theta);
        // 对 p,q 两行两列做旋转
        for (int i = 0; i < n; i++) {
            double mip = M(i, p), miq = M(i, q);
            M(i, p) = c * mip - s * miq;
            M(i, q) = s * mip + c * miq;
            double vip = V(i, p), viq = V(i, q);
            V(i, p) = c * vip - s * viq;
            V(i, q) = s * vip + c * viq;
        }
        for (int j = 0; j < n; j++) {
            double mpj = M(p, j), mqj = M(q, j);
            M(p, j) = c * mpj - s * mqj;
            M(q, j) = s * mpj + c * mqj;
        }
    }
    for (int i = 0; i < n; i++) out_eig[i] = M(i, i);
    if (out_V) *out_V = V;
    return true;
}
// ---------------- 向量运算 ----------------
double vec_dot(const double* a, const double* b, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

double vec_norm(const double* a, int n) {
    return std::sqrt(vec_dot(a, a, n));
}

void vec_normalize(double* a, int n) {
    double v = vec_norm(a, n);
    if (v < 1e-300) return;
    for (int i = 0; i < n; i++) a[i] /= v;
}

double vec_cross2d(const double* a, const double* b) {
    return a[0] * b[1] - a[1] * b[0];
}

// ---------------- 最小二乘 / 伪逆 ----------------
double* lstsq_solve(const Matrix& A, const double* b) {
    // 正规方程：(A^T A) x = A^T b
    Matrix At = mat_transpose(A);
    Matrix AtA = mat_mul(At, A);
    double* Atb = new double[(size_t)A.cols];
    for (int i = 0; i < A.cols; i++) {
        double s = 0.0;
        for (int j = 0; j < A.rows; j++) s += At(i, j) * b[j];
        Atb[i] = s;
    }
    double* x = mat_solve(AtA, Atb);
    delete[] Atb;
    return x;
}

Matrix mat_pinv(const Matrix& A) {
    Matrix At = mat_transpose(A);
    Matrix AtA = mat_mul(At, A);
    Matrix AtAinv = mat_inverse(AtA);
    if (!AtAinv.valid()) return Matrix();
    return mat_mul(AtAinv, At);
}
// ---------------- 自检 ----------------
int matrix_self_test() {
    int fails = 0;
    const double EPS = 1e-8;

    // 1. det([[1,2],[3,4]]) = -2
    double a2[4] = {1, 2, 3, 4};
    Matrix A(2, 2, a2);
    if (!d_close(mat_determinant(A), -2.0, EPS)) fails++;

    // 2. inv([[1,2],[3,4]]) = [[-2,1],[1.5,-0.5]]
    Matrix Ai = mat_inverse(A);
    if (!Ai.valid()) fails++;
    else {
        double exp_inv[4] = {-2, 1, 1.5, -0.5};
        for (int i = 0; i < 4; i++)
            if (!d_close(Ai.m[i], exp_inv[i], EPS)) fails++;
        Matrix I = mat_mul(A, Ai);
        Matrix Ie = Matrix::identity(2);
        for (int i = 0; i < 4; i++)
            if (!d_close(I.m[i], Ie.m[i], EPS)) fails++;
    }

    // 3. 转置
    double t2[6] = {1, 2, 3, 4, 5, 6};
    Matrix B(2, 3, t2);
    Matrix Bt = mat_transpose(B);
    if (Bt.rows != 3 || Bt.cols != 2) fails++;
    else {
        if (!d_close(Bt(0, 1), 4.0, EPS)) fails++;
        if (!d_close(Bt(2, 0), 3.0, EPS)) fails++;
    }

    // 4. 解方程组 [[2,0],[0,1]] x = [4,3] -> x=[2,3]
    double s2[4] = {2, 0, 0, 1};
    double b2[2] = {4, 3};
    Matrix S(2, 2, s2);
    double* x = mat_solve(S, b2);
    if (!x) fails++;
    else {
        if (!d_close(x[0], 2.0, EPS)) fails++;
        if (!d_close(x[1], 3.0, EPS)) fails++;
        delete[] x;
    }

    // 5. 迹：trace(1,2;3,4) = 5
    if (!d_close(mat_trace(A), 5.0, EPS)) fails++;

    // 6. 秩：[[1,2],[2,4]] 秩 1；单位阵秩 3
    double r1[4] = {1, 2, 2, 4};
    Matrix R1(2, 2, r1);
    if (mat_rank(R1) != 1) fails++;
    if (mat_rank(Matrix::identity(3)) != 3) fails++;

    // 7. QR：Q^T Q ≈ I（3x3）
    double q3[9] = {1, 2, 3,
                    4, 5, 6,
                    7, 8, 10};
    Matrix QM(3, 3, q3);
    Matrix Q, R;
    if (!qr_decompose(QM, Q, R)) fails++;
    else {
        Matrix QtQ = mat_mul(mat_transpose(Q), Q);
        Matrix I3 = Matrix::identity(3);
        for (int i = 0; i < 9; i++)
            if (!d_close(QtQ.m[i], I3.m[i], 1e-6)) fails++;
    }

    // 8. Cholesky：SPD 矩阵 [[4,2],[2,5]]，L = [[2,0],[1,2]]（L*L^T 验证）
    double c2[4] = {4, 2, 2, 5};
    Matrix CM(2, 2, c2);
    Matrix Lc;
    bool cholok = cholesky_decompose(CM, Lc);
    if (!cholok) fails++;
    else {
        Matrix Lt = mat_transpose(Lc);
        Matrix LLt = mat_mul(Lc, Lt);
        for (int i = 0; i < 4; i++)
            if (!d_close(LLt.m[i], CM.m[i], EPS)) fails++;
    }

    // 9. 幂迭代：对角阵 diag(2,1) 主特征值 = 2
    double d2[4] = {2, 0, 0, 1};
    Matrix D(2, 2, d2);
    double* vec = new double[2];
    double eig = eigen_power(D, vec);
    if (!d_close(eig, 2.0, 1e-6)) fails++;
    // 逆幂迭代：模最小特征值 = 1
    double eig2 = eigen_inv_power(D, vec);
    if (!d_close(eig2, 1.0, 1e-6)) fails++;
    delete[] vec;

    return fails;
}

} // namespace mathext
} // namespace nefu
