// nefuOS mathlib —— 矩阵实现 + 自测
#include "mathlib/matrix.h"
#include <cstdio>

namespace nefu {
namespace mathx {

Matrix::Matrix() : rows(1), cols(1) { memset(m, 0, sizeof(m)); }
Matrix::Matrix(int r, int c) : rows(r), cols(c) { memset(m, 0, sizeof(m)); }

Matrix Matrix::identity(int n) {
    Matrix r(n, n);
    for (int i = 0; i < n; i++) r.m[i][i] = 1.0;
    return r;
}

Matrix& Matrix::add(const Matrix& b) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) m[i][j] += b.m[i][j];
    return *this;
}

Matrix& Matrix::sub(const Matrix& b) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) m[i][j] -= b.m[i][j];
    return *this;
}

Matrix Matrix::mul(const Matrix& b) const {
    Matrix r(rows, b.cols);
    for (int i = 0; i < rows; i++)
        for (int k = 0; k < cols; k++)
            if (m[i][k] != 0)
                for (int j = 0; j < b.cols; j++)
                    r.m[i][j] += m[i][k] * b.m[k][j];
    return r;
}

Matrix Matrix::transposed() const {
    Matrix r(cols, rows);
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) r.m[j][i] = m[i][j];
    return r;
}

Matrix Matrix::scaled(double k) const {
    Matrix r = *this;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++) r.m[i][j] *= k;
    return r;
}

double Matrix::det() const {
    if (rows != cols) return 0;
    if (rows == 1) return m[0][0];
    if (rows == 2) return m[0][0] * m[1][1] - m[0][1] * m[1][0];
    // 递归按第一行展开
    double sum = 0;
    for (int j = 0; j < rows; j++) {
        Matrix sub(rows - 1, rows - 1);
        int si = 0;
        for (int i = 1; i < rows; i++) {
            int sj = 0;
            for (int k = 0; k < rows; k++) {
                if (k == j) continue;
                sub.m[si][sj++] = m[i][k];
            }
            si++;
        }
        double term = m[0][j] * sub.det();
        if (j % 2 == 1) term = -term;
        sum += term;
    }
    return sum;
}

Matrix Matrix::inverse() const {
    Matrix r(rows, cols);
    double d = det();
    if (d == 0 || rows != cols) return r;   // 不可逆返回零矩阵
    // 伴随矩阵 / det
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < rows; j++) {
            Matrix sub(rows - 1, rows - 1);
            int si = 0;
            for (int r0 = 0; r0 < rows; r0++) {
                if (r0 == i) continue;
                int sj = 0;
                for (int c0 = 0; c0 < rows; c0++) {
                    if (c0 == j) continue;
                    sub.m[si][sj++] = m[r0][c0];
                }
                si++;
            }
            double c = sub.det();
            if ((i + j) % 2 == 1) c = -c;
            r.m[j][i] = c / d;   // 转置放置
        }
    }
    return r;
}

void Matrix::fill(double v) {
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++) m[i][j] = v;
}

bool Matrix::near(const Matrix& b, double eps) const {
    if (rows != b.rows || cols != b.cols) return false;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            if ((m[i][j] - b.m[i][j]) > eps || (b.m[i][j] - m[i][j]) > eps) return false;
    return true;
}

// ---- self test ----
namespace {
int g_fails = 0;
void expect(const char* what, bool ok) { if (!ok) { g_fails++; printf("FAIL: %s\n", what); } (void)what; }
}

int matrix_self_test() {
    g_fails = 0;
    {
        Matrix a(2, 2), b(2, 2);
        a.m[0][0] = 1; a.m[0][1] = 2; a.m[1][0] = 3; a.m[1][1] = 4;
        b.m[0][0] = 5; b.m[0][1] = 6; b.m[1][0] = 7; b.m[1][1] = 8;
        Matrix aa = a;
        Matrix s = aa.add(b);
        expect("matrix-add", s.m[0][0] == 6 && s.m[1][1] == 12);
        Matrix p = a.mul(b);
        expect("matrix-mul", p.m[0][0] == 19 && p.m[0][1] == 22 && p.m[1][0] == 43 && p.m[1][1] == 50);
        Matrix t = a.transposed();
        expect("matrix-transpose", t.m[0][1] == 3 && t.m[1][0] == 2);
    }
    {
        Matrix a(2, 2);
        a.m[0][0] = 1; a.m[0][1] = 2; a.m[1][0] = 3; a.m[1][1] = 4;
        expect("matrix-det", a.det() == -2);
        Matrix inv = a.inverse();
        Matrix prod = a.mul(inv);
        Matrix id = Matrix::identity(2);
        expect("matrix-inverse", prod.near(id, 1e-9));
    }
    {
        Matrix a(3, 3);
        a.m[0][0] = 1; a.m[0][1] = 2; a.m[0][2] = 3;
        a.m[1][0] = 0; a.m[1][1] = 1; a.m[1][2] = 4;
        a.m[2][0] = 5; a.m[2][1] = 6; a.m[2][2] = 0;
        expect("matrix-det3", a.det() == 1);
        Matrix id = Matrix::identity(3);
        Matrix inv = a.inverse();
        expect("matrix-inv3", a.mul(inv).near(id, 1e-9));
    }
    return g_fails;
}

} // namespace mathx
} // namespace nefu
