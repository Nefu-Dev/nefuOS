#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— 定点稠密矩阵实现（Q16.16，全整数运算）
#include "matrix.h"
#include <cmath>   // 仅 from_doubles（self_test 便利）与近似比较用，算法本体无 FPU
#include <cstdio>

namespace nefu {
namespace ml {

// ---------------- 生命周期 ----------------
FxMat::FxMat() : rows(0), cols(0), m(0) {}

FxMat::FxMat(int r, int c) : rows(r), cols(c), m(0) {
    if (r > 0 && c > 0) m = new fix[(size_t)(r * c)];
}

FxMat::FxMat(int r, int c, const fix* flat) : rows(r), cols(c), m(0) {
    if (r > 0 && c > 0) {
        m = new fix[(size_t)(r * c)];
        int n = r * c;
        for (int i = 0; i < n; i++) m[i] = flat[i];   // 朴素拷贝循环
    }
}

FxMat::FxMat(const FxMat& o) : rows(o.rows), cols(o.cols), m(0) {
    if (o.valid()) {
        m = new fix[(size_t)(rows * cols)];
        int n = rows * cols;
        for (int i = 0; i < n; i++) m[i] = o.m[i];
    }
}

FxMat::~FxMat() {
    if (m) delete[] m;
    m = 0; rows = 0; cols = 0;
}

FxMat& FxMat::operator=(const FxMat& o) {
    if (this != &o) {
        if (m) delete[] m;
        rows = o.rows; cols = o.cols; m = 0;
        if (o.valid()) {
            m = new fix[(size_t)(rows * cols)];
            int n = rows * cols;
            for (int i = 0; i < n; i++) m[i] = o.m[i];
        }
    }
    return *this;
}

void FxMat::set_all(fix v) {
    if (!valid()) return;
    int n = rows * cols;
    for (int i = 0; i < n; i++) m[i] = v;
}

FxMat FxMat::identity(int n) {
    FxMat R(n, n);
    if (!R.valid()) return R;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            R(i, j) = (i == j) ? fx::FX_ONE : 0;
    return R;
}

FxMat FxMat::from_doubles(int r, int c, const double* fv) {
    FxMat R(r, c);
    if (!R.valid()) return R;
    int n = r * c;
    for (int i = 0; i < n; i++) {
        double v = fv[i];
        R.m[i] = (fix)(v * 65536.0);    // double->Q16.16（仅测试便利）
    }
    return R;
}

// ---------------- 基本运算 ----------------
FxMat mat_add(const FxMat& A, const FxMat& B) {
    if (!A.valid() || !B.valid() || A.rows != B.rows || A.cols != B.cols) return FxMat();
    FxMat R(A.rows, A.cols);
    int n = A.rows * A.cols;
    for (int i = 0; i < n; i++) R.m[i] = A.m[i] + B.m[i];
    return R;
}

FxMat mat_sub(const FxMat& A, const FxMat& B) {
    if (!A.valid() || !B.valid() || A.rows != B.rows || A.cols != B.cols) return FxMat();
    FxMat R(A.rows, A.cols);
    int n = A.rows * A.cols;
    for (int i = 0; i < n; i++) R.m[i] = A.m[i] - B.m[i];
    return R;
}

FxMat mat_mul(const FxMat& A, const FxMat& B) {
    if (!A.valid() || !B.valid() || A.cols != B.rows) return FxMat();
    FxMat C(A.rows, B.cols);
    for (int i = 0; i < A.rows; i++) {
        for (int j = 0; j < B.cols; j++) {
            fix64 s = 0;
            for (int k = 0; k < A.cols; k++)
                s += (fix64)A(i, k) * (fix64)B(k, j);   // Q32.32 累加
            C(i, j) = (fix)(s >> 16);
        }
    }
    return C;
}

FxMat mat_scale(const FxMat& A, fix s) {
    if (!A.valid()) return FxMat();
    FxMat R(A.rows, A.cols);
    int n = A.rows * A.cols;
    for (int i = 0; i < n; i++) R.m[i] = fx::fx_mul(A.m[i], s);
    return R;
}

FxMat mat_transpose(const FxMat& A) {
    if (!A.valid()) return FxMat();
    FxMat R(A.cols, A.rows);
    for (int i = 0; i < A.rows; i++)
        for (int j = 0; j < A.cols; j++)
            R(j, i) = A(i, j);
    return R;
}

// ---------------- 行列式（LU + 部分选主元） ----------------
fix mat_determinant(const FxMat& A) {
    if (!A.valid() || A.rows != A.cols) return 0;
    int n = A.rows;
    // 拷贝到工作区
    fix* m = new fix[(size_t)(n * n)];
    for (int i = 0; i < n * n; i++) m[i] = A.m[i];

    fix det = fx::FX_ONE;
    for (int c = 0; c < n; c++) {
        // 选主元
        int piv = c;
        fix best = m[c * n + c] < 0 ? -m[c * n + c] : m[c * n + c];
        for (int r = c + 1; r < n; r++) {
            fix v = m[r * n + c] < 0 ? -m[r * n + c] : m[r * n + c];
            if (v > best) { best = v; piv = r; }
        }
        if (best == 0) { delete[] m; return 0; }     // 奇异
        if (piv != c) {
            for (int j = 0; j < n; j++) {
                fix t = m[c * n + j]; m[c * n + j] = m[piv * n + j]; m[piv * n + j] = t;
            }
            det = -det;
        }
        fix diag = m[c * n + c];
        det = fx::fx_mul(det, diag);
        // 消去下方
        for (int r = c + 1; r < n; r++) {
            fix f = fx::fx_div(m[r * n + c], diag);
            for (int j = c; j < n; j++) {
                m[r * n + j] -= fx::fx_mul(f, m[c * n + j]);
            }
        }
    }
    delete[] m;
    return det;
}

// ---------------- 求逆（Gauss-Jordan） ----------------
FxMat mat_inverse(const FxMat& A) {
    if (!A.valid() || A.rows != A.cols) return FxMat();
    int n = A.rows;
    // 增广矩阵 [A | I]，n 行 2n 列
    fix* aug = new fix[(size_t)(n * 2 * n)];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            aug[i * 2 * n + j] = A(i, j);
            aug[i * 2 * n + n + j] = (i == j) ? fx::FX_ONE : 0;
        }
    for (int c = 0; c < n; c++) {
        int piv = c;
        fix best = aug[c * 2 * n + c] < 0 ? -aug[c * 2 * n + c] : aug[c * 2 * n + c];
        for (int r = c + 1; r < n; r++) {
            fix v = aug[r * 2 * n + c] < 0 ? -aug[r * 2 * n + c] : aug[r * 2 * n + c];
            if (v > best) { best = v; piv = r; }
        }
        if (best == 0) { delete[] aug; return FxMat(); }   // 奇异
        if (piv != c) {
            for (int j = 0; j < 2 * n; j++) {
                fix t = aug[c * 2 * n + j]; aug[c * 2 * n + j] = aug[piv * 2 * n + j]; aug[piv * 2 * n + j] = t;
            }
        }
        fix d = aug[c * 2 * n + c];
        for (int j = 0; j < 2 * n; j++) aug[c * 2 * n + j] = fx::fx_div(aug[c * 2 * n + j], d);
        for (int r = 0; r < n; r++) {
            if (r == c) continue;
            fix f = aug[r * 2 * n + c];
            for (int j = 0; j < 2 * n; j++)
                aug[r * 2 * n + j] -= fx::fx_mul(f, aug[c * 2 * n + j]);
        }
    }
    FxMat R(n, n);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            R(i, j) = aug[i * 2 * n + n + j];
    delete[] aug;
    return R;
}

fix mat_trace(const FxMat& A) {
    if (!A.valid() || A.rows != A.cols) return 0;
    fix s = 0;
    for (int i = 0; i < A.rows; i++) s += A(i, i);
    return s;
}

// ---------------- 解 A x = b（高斯消元 + 部分选主元） ----------------
fix* mat_solve(const FxMat& A, const fix* b) {
    if (!A.valid() || A.rows != A.cols) return 0;
    int n = A.rows;
    fix* ab = new fix[(size_t)(n * (n + 1))];
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) ab[i * (n + 1) + j] = A(i, j);
        ab[i * (n + 1) + n] = b[i];
    }
    for (int c = 0; c < n; c++) {
        int piv = c;
        fix best = ab[c * (n + 1) + c] < 0 ? -ab[c * (n + 1) + c] : ab[c * (n + 1) + c];
        for (int r = c + 1; r < n; r++) {
            fix v = ab[r * (n + 1) + c] < 0 ? -ab[r * (n + 1) + c] : ab[r * (n + 1) + c];
            if (v > best) { best = v; piv = r; }
        }
        if (best == 0) { delete[] ab; return 0; }
        if (piv != c) {
            for (int j = 0; j <= n; j++) {
                fix t = ab[c * (n + 1) + j]; ab[c * (n + 1) + j] = ab[piv * (n + 1) + j]; ab[piv * (n + 1) + j] = t;
            }
        }
        fix d = ab[c * (n + 1) + c];
        for (int r = c + 1; r < n; r++) {
            fix f = fx::fx_div(ab[r * (n + 1) + c], d);
            for (int j = c; j <= n; j++)
                ab[r * (n + 1) + j] -= fx::fx_mul(f, ab[c * (n + 1) + j]);
        }
    }
    // 回代
    fix* x = new fix[n];
    for (int i = n - 1; i >= 0; i--) {
        fix s = ab[i * (n + 1) + n];
        for (int j = i + 1; j < n; j++) s -= fx::fx_mul(ab[i * (n + 1) + j], x[j]);
        x[i] = fx::fx_div(s, ab[i * (n + 1) + i]);
    }
    delete[] ab;
    return x;
}

// ---------------- 对称矩阵 Jacobi 特征值 ----------------
bool jacobi_eigen(const FxMat& A, fix* out_eig, FxMat* out_V, int max_iter, fix tol) {
    if (!A.valid() || A.rows != A.cols) return false;
    int n = A.rows;
    FxMat M = A;                       // 拷贝
    FxMat V = FxMat::identity(n);
    for (int it = 0; it < max_iter; it++) {
        // 找最大非对角元
        int p = 0, q = 1;
        fix best = 0;
        for (int i = 0; i < n; i++)
            for (int j = i + 1; j < n; j++) {
                fix v = M(i, j) < 0 ? -M(i, j) : M(i, j);
                if (v > best) { best = v; p = i; q = j; }
            }
        if (best <= tol) break;
        // theta = 0.5 * atan2(2*M[p][q], M[q][q]-M[p][p])
        fix two_mij = fx::fx_mul(M(p, q), fx::itofix(2));
        fix diff = M(q, q) - M(p, p);
        fix theta = fx::fx_div(fx::fx_atan2(two_mij, diff), fx::itofix(2));
        fix c = fx::fx_cos(theta);
        fix s = fx::fx_sin(theta);
        // 旋转 p,q 两列（先列后行）
        for (int i = 0; i < n; i++) {
            fix mip = M(i, p), miq = M(i, q);
            M(i, p) = fx::fx_mul(c, mip) - fx::fx_mul(s, miq);
            M(i, q) = fx::fx_mul(s, mip) + fx::fx_mul(c, miq);
            fix vip = V(i, p), viq = V(i, q);
            V(i, p) = fx::fx_mul(c, vip) - fx::fx_mul(s, viq);
            V(i, q) = fx::fx_mul(s, vip) + fx::fx_mul(c, viq);
        }
        for (int j = 0; j < n; j++) {
            fix mpj = M(p, j), mqj = M(q, j);
            M(p, j) = fx::fx_mul(c, mpj) - fx::fx_mul(s, mqj);
            M(q, j) = fx::fx_mul(s, mpj) + fx::fx_mul(c, mqj);
        }
    }
    for (int i = 0; i < n; i++) out_eig[i] = M(i, i);
    if (out_V) *out_V = V;
    return true;
}

// ---------------- 范数 ----------------
fix mat_norm_frobenius(const FxMat& A) {
    if (!A.valid()) return 0;
    fix64 s = 0;
    int n = A.rows * A.cols;
    for (int i = 0; i < n; i++) s += (fix64)A.m[i] * (fix64)A.m[i];
    fix arg = (fix)(s >> 16);
    if (arg < 0) arg = 0;
    return fx::fx_sqrt(arg);
}

// ---------------- LU 分解（Doolittle） ----------------
bool lu_decompose(const FxMat& A, FxMat& L, FxMat& U) {
    if (!A.valid() || A.rows != A.cols) return false;
    int n = A.rows;
    L = FxMat::identity(n);
    U = FxMat(n, n);
    U.set_all(0);
    for (int i = 0; i < n; i++) {
        for (int j = i; j < n; j++) {
            fix64 s = 0;
            for (int k = 0; k < i; k++) s += (fix64)L(i, k) * (fix64)U(k, j);
            U(i, j) = A(i, j) - (fix)(s >> 16);
        }
        if (U(i, i) == 0) return false;
        for (int j = i + 1; j < n; j++) {
            fix64 s = 0;
            for (int k = 0; k < i; k++) s += (fix64)L(j, k) * (fix64)U(k, i);
            fix num = A(j, i) - (fix)(s >> 16);
            L(j, i) = fx::fx_div(num, U(i, i));
        }
    }
    return true;
}

// ---------------- QR 分解（修正 Gram-Schmidt） ----------------
bool qr_decompose(const FxMat& A, FxMat& Q, FxMat& R) {
    if (!A.valid()) return false;
    int m = A.rows, n = A.cols;
    Q = FxMat(m, n);
    R = FxMat(n, n);
    R.set_all(0);
    for (int k = 0; k < n; k++) {
        fix* v = new fix[m];
        for (int i = 0; i < m; i++) v[i] = A(i, k);
        for (int j = 0; j < k; j++) {
            fix64 s = 0;
            for (int i = 0; i < m; i++) s += (fix64)Q(i, j) * (fix64)A(i, k);
            fix rjk = (fix)(s >> 16);
            R(j, k) = rjk;
            for (int i = 0; i < m; i++) v[i] -= fx::fx_mul(rjk, Q(i, j));
        }
        fix nv = l2norm(v, m);
        if (nv == 0) { delete[] v; return false; }
        R(k, k) = nv;
        for (int i = 0; i < m; i++) Q(i, k) = fx::fx_div(v[i], nv);
        delete[] v;
    }
    return true;
}

// ---------------- Cholesky 分解（A = L L^T） ----------------
bool cholesky_decompose(const FxMat& A, FxMat& L) {
    if (!A.valid() || A.rows != A.cols) return false;
    int n = A.rows;
    L = FxMat(n, n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            fix64 s = 0;
            for (int k = 0; k < j; k++) s += (fix64)L(i, k) * (fix64)L(j, k);
            fix val = A(i, j) - (fix)(s >> 16);
            if (i == j) {
                if (val <= 0) return false;
                L(i, j) = fx::fx_sqrt(val);
            } else {
                L(i, j) = fx::fx_div(val, L(j, j));
            }
        }
    }
    return true;
}

// ---------------- 秩（行梯形） ----------------
int mat_rank(const FxMat& A) {
    if (!A.valid()) return 0;
    FxMat M = A;
    int rows = M.rows, cols = M.cols;
    int r = 0;
    for (int c = 0; c < cols && r < rows; c++) {
        int piv = r;
        fix best = M(r, c) < 0 ? -M(r, c) : M(r, c);
        for (int i = r + 1; i < rows; i++) {
            fix v = M(i, c) < 0 ? -M(i, c) : M(i, c);
            if (v > best) { best = v; piv = i; }
        }
        if (best == 0) continue;
        if (piv != r) {
            for (int j = 0; j < cols; j++) {
                fix t = M(r, j); M(r, j) = M(piv, j); M(piv, j) = t;
            }
        }
        for (int i = r + 1; i < rows; i++) {
            fix f = fx::fx_div(M(i, c), M(r, c));
            for (int j = c; j < cols; j++) M(i, j) -= fx::fx_mul(f, M(r, j));
        }
        r++;
    }
    return r;
}

// ---------------- 条件数（无穷范数） ----------------
static fix mat_norm_inf(const FxMat& A) {
    fix best = 0;
    for (int i = 0; i < A.rows; i++) {
        fix s = 0;
        for (int j = 0; j < A.cols; j++) { fix v = A(i,j)<0?-A(i,j):A(i,j); s += v; }
        if (s > best) best = s;
    }
    return best;
}
fix mat_cond_number(const FxMat& A) {
    FxMat Ai = mat_inverse(A);
    if (!Ai.valid()) return -1;
    return fx::fx_mul(mat_norm_inf(A), mat_norm_inf(Ai));
}

// ---------------- 矩阵幂（快速幂） ----------------
FxMat mat_pow(const FxMat& A, int k) {
    if (!A.valid() || A.rows != A.cols) return FxMat();
    FxMat result = FxMat::identity(A.rows);
    FxMat base = A;
    int e = k;
    while (e > 0) {
        if (e & 1) result = mat_mul(result, base);
        base = mat_mul(base, base);
        e >>= 1;
    }
    return result;
}
// ---------------- 自检 ----------------
int matrix_self_test() {
    int fails = 0;
    const fix TOL = fx::fxf(2, 100);          // 0.02，定点误差容差
    const fix TOL2 = fx::fxf(10, 100);         // 0.10，分解类算法宽松容差

    // 1. det([[1,2],[3,4]]) = -2
    double d2[4] = {1, 2, 3, 4};
    FxMat A = FxMat::from_doubles(2, 2, d2);
    fix detA = mat_determinant(A);
    if (!fx_close(detA, -fx::FX_TWO, TOL)) fails++;

    // 2. inv([[1,2],[3,4]]) = [[-2,1],[1.5,-0.5]]，且 A*A^-1 ≈ I
    FxMat Ai = mat_inverse(A);
    if (!Ai.valid()) { fails++; }
    else {
        double exp_inv[4] = {-2, 1, 1.5, -0.5};
        FxMat Ei = FxMat::from_doubles(2, 2, exp_inv);
        for (int i = 0; i < 4; i++)
            if (!fx_close(Ai.m[i], Ei.m[i], TOL)) fails++;
        FxMat I = mat_mul(A, Ai);
        FxMat Ie = FxMat::identity(2);
        for (int i = 0; i < 4; i++)
            if (!fx_close(I.m[i], Ie.m[i], TOL)) fails++;
    }

    // 3. 转置：[[1,2,3],[4,5,6]] -> [[1,4],[2,5],[3,6]]
    double t2[6] = {1, 2, 3, 4, 5, 6};
    FxMat B = FxMat::from_doubles(2, 3, t2);
    FxMat Bt = mat_transpose(B);
    if (Bt.rows != 3 || Bt.cols != 2) fails++;
    else {
        if (!fx_close(Bt(0, 1), fx::FX_ONE * 4, TOL)) fails++;
        if (!fx_close(Bt(2, 0), fx::FX_ONE * 3, TOL)) fails++;
    }

    // 4. 解 diag(2,1) x = [4,3] -> x=[2,3]
    double s2[4] = {2, 0, 0, 1};
    double sb[2] = {4, 3};
    FxMat S = FxMat::from_doubles(2, 2, s2);
    fix sbx[2] = {FxMat::from_doubles(2, 1, sb).m[0], FxMat::from_doubles(2, 1, sb).m[1]};
    fix* x = mat_solve(S, sbx);
    if (!x) fails++;
    else {
        if (!fx_close(x[0], fx::FX_TWO, TOL)) fails++;
        if (!fx_close(x[1], fx::FX_ONE * 3, TOL)) fails++;
        delete[] x;
    }

    // 5. 加减乘：[1,2]+[3,4]=[4,6]；[1,2;3,4]*[1;1]=[3;7]
    double r1[2] = {1, 2}, r2[2] = {3, 4};
    FxMat R1 = FxMat::from_doubles(1, 2, r1);
    FxMat R2 = FxMat::from_doubles(1, 2, r2);
    FxMat RS = mat_add(R1, R2);
    if (!fx_close(RS.m[0], fx::FX_ONE * 4, TOL)) fails++;
    if (!fx_close(RS.m[1], fx::FX_ONE * 6, TOL)) fails++;
    FxMat col = FxMat::from_doubles(2, 1, r1);   // [1;2]
    FxMat prod = mat_mul(A, col);                // [[1,2],[3,4]]*[1;2] = [5;11]
    if (!fx_close(prod(0, 0), fx::FX_ONE * 5, TOL)) fails++;
    if (!fx_close(prod(1, 0), fx::FX_ONE * 11, TOL)) fails++;

    // 6. 迹 trace([[1,2],[3,4]]) = 5
    if (!fx_close(mat_trace(A), fx::FX_ONE * 5, TOL)) fails++;

    // 7. Jacobi：对角阵 diag(3,1) 特征值应为 3,1（顺序可能不同）
    double d3[4] = {3, 0, 0, 1};
    FxMat D = FxMat::from_doubles(2, 2, d3);
    fix eig[2];
    if (!jacobi_eigen(D, eig, 0, 50, 16)) fails++;
    else {
        // 两个特征值应分别接近 1 和 3
        fix lo = fx_close(eig[0], fx::FX_ONE, TOL) || fx_close(eig[1], fx::FX_ONE, TOL);
        fix hi = fx_close(eig[0], fx::FX_ONE * 3, TOL) || fx_close(eig[1], fx::FX_ONE * 3, TOL);
        if (!lo || !hi) fails++;
    }
    // 8. LU 分解：[[2,3],[1,2]] -> L*U = A
    double lu_a[4] = {2, 3, 1, 2};
    FxMat LU = FxMat::from_doubles(2, 2, lu_a);
    FxMat L, U;
    if (!lu_decompose(LU, L, U)) fails++;
    else {
        FxMat LU2 = mat_mul(L, U);
        for (int i = 0; i < 4; i++)
            if (!fx_close(LU2.m[i], LU.m[i], TOL)) fails++;
    }

    // 9. QR 分解：A = Q*R，Q^T Q ≈ I
    double qr_a[6] = {1, 2, 3, 4, 5, 6};
    FxMat QA = FxMat::from_doubles(3, 2, qr_a);
    FxMat Q, Rm;
    if (!qr_decompose(QA, Q, Rm)) fails++;
    else {
        FxMat QQt = mat_mul(mat_transpose(Q), Q);
        // Q^T Q 应近似 2x2 单位阵
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                fix want = (i == j) ? fx::FX_ONE : 0;
                if (!fx_close(QQt(i, j), want, TOL2)) fails++;
            }
        FxMat QR = mat_mul(Q, Rm);
        for (int i = 0; i < 6; i++)
            if (!fx_close(QR.m[i], QA.m[i], TOL2)) fails++;
    }

    // 10. Cholesky：[[4,2],[2,5]] = L L^T
    double ch_a[4] = {4, 2, 2, 5};
    FxMat CA = FxMat::from_doubles(2, 2, ch_a);
    FxMat CL;
    if (!cholesky_decompose(CA, CL)) fails++;
    else {
        FxMat CCT = mat_mul(CL, mat_transpose(CL));
        for (int i = 0; i < 4; i++)
            if (!fx_close(CCT.m[i], CA.m[i], TOL2)) fails++;
    }

    // 11. 秩：[[1,2],[2,4]] 秩为1（线性相关）；[[1,2],[3,4]] 秩为2
    double rk1[4] = {1, 2, 2, 4};
    FxMat RankM = FxMat::from_doubles(2, 2, rk1);
    if (mat_rank(RankM) != 1) fails++;
    if (mat_rank(A) != 2) fails++;

    // 12. 矩阵幂：[[1,1],[0,1]]^3 = [[1,3],[0,1]]
    double p_a[4] = {1, 1, 0, 1};
    FxMat PA = FxMat::from_doubles(2, 2, p_a);
    FxMat P3 = mat_pow(PA, 3);
    if (!fx_close(P3(0, 1), fx::FX_ONE * 3, TOL)) fails++;
    if (!fx_close(P3(1, 0), 0, TOL)) fails++;

    return fails;
}

} // namespace ml
} // namespace nefu
