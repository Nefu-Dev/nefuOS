#pragma GCC optimize("no-tree-loop-distribute-patterns")
// nefuOS 机器学习库 —— PCA 实现（Q16.16）
#include "pca.h"
#include <cmath>

namespace nefu {
namespace ml {

Pca::Pca() : d(0), k(0), mean(0), comps(0), eigvals(0) {}
Pca::~Pca() { release(); }
void Pca::release() {
    if (mean) delete[] mean;
    if (comps) delete[] comps;
    if (eigvals) delete[] eigvals;
    mean = comps = 0; eigvals = 0; d = 0; k = 0;
}

void Pca::fit(const fix* Xflat, int n, int d_, int k_) {
    release();
    d = d_; k = k_;
    mean = new fix[d];
    eigvals = new fix[d];

    // 1. 每维均值
    for (int j = 0; j < d; j++) {
        fix64 s = 0;
        for (int i = 0; i < n; i++) s += (fix64)Xflat[(size_t)i * d + j];
        mean[j] = (fix)(s / (fix64)n);
    }

    // 2. 协方差矩阵 C[d][d]（对称）
    FxMat C(d, d);
    for (int i = 0; i < d; i++)
        for (int j = i; j < d; j++) {
            fix64 s = 0;
            for (int a = 0; a < n; a++) {
                fix xi = Xflat[(size_t)a * d + i] - mean[i];
                fix xj = Xflat[(size_t)a * d + j] - mean[j];
                s += (fix64)xi * (fix64)xj;
            }
            fix v = fx::fx_div((fix)(s >> 16), fx::itofix(n));
            C(i, j) = v; C(j, i) = v;
        }

    // 3. Jacobi 特征值分解
    FxMat V;
    jacobi_eigen(C, eigvals, &V, 100, 32);   // tol ≈ 0.0005

    // 4. 按特征值降序排序（简单选择排序），取出前 k 个特征向量
    int* order = new int[d];
    for (int i = 0; i < d; i++) order[i] = i;
    for (int i = 0; i < d - 1; i++) {
        int best = i;
        for (int j = i + 1; j < d; j++)
            if (eigvals[order[j]] > eigvals[order[best]]) best = j;
        int t = order[i]; order[i] = order[best]; order[best] = t;
    }
    comps = new fix[(size_t)d * k];
    // 同步把 eigvals 按 order 重排，保证 explained_ratio 读到的是降序特征值
    fix* sorted_ev = new fix[(size_t)d];
    for (int c = 0; c < d; c++) sorted_ev[c] = eigvals[order[c]];
    for (int c = 0; c < d; c++) eigvals[c] = sorted_ev[c];
    delete[] sorted_ev;
    for (int c = 0; c < k; c++) {
        int eig_idx = order[c];
        for (int j = 0; j < d; j++)
            comps[(size_t)c * d + j] = V(j, eig_idx);   // 特征向量是 V 的第 eig_idx 列
    }
    delete[] order;
}

void Pca::transform(const fix* x, fix* out) const {
    // 中心化后投影：out[c] = Σ_j (x[j]-mean[j]) * comps[c][j]
    for (int c = 0; c < k; c++) {
        fix64 s = 0;
        for (int j = 0; j < d; j++) {
            fix xj = x[j] - mean[j];
            s += (fix64)xj * (fix64)comps[(size_t)c * d + j];
        }
        out[c] = (fix)(s >> 16);
    }
}

void Pca::inverse_transform(const fix* z, fix* out) const {
    // out[j] = mean[j] + Σ_c z[c] * comps[c][j]
    for (int j = 0; j < d; j++) out[j] = mean[j];
    for (int c = 0; c < k; c++) {
        fix zc = z[c];
        for (int j = 0; j < d; j++)
            out[j] += fx::fx_mul(zc, comps[(size_t)c * d + j]);
    }
}

void Pca::whiten(const fix* x, fix* out) const {
    transform(x, out);
    for (int c = 0; c < k; c++) {
        fix ev = eigvals[c] > 0 ? eigvals[c] : 1;
        fix scale = fx::fx_div(fx::FX_ONE, fx::fx_sqrt(ev));
        out[c] = fx::fx_mul(out[c], scale);
    }
}
fix Pca::explained_ratio(int i) const {
    if (i < 0 || i >= k) return 0;
    fix64 total = 0;
    for (int j = 0; j < d; j++) total += (fix64)(eigvals[j] > 0 ? eigvals[j] : 0);
    if (total <= 0) return 0;
    fix ev = eigvals[i] > 0 ? eigvals[i] : 0;
    return fx::fx_div(ev, (fix)(total >> 16));
}

// ---------------- 自检 ----------------
int pca_self_test() {
    int fails = 0;

    // 沿 y=x 方向拉长的点：(0,0),(1,1),(2,2),(3,3),(4,4) + 微小垂直扰动
    // 主成分 1 应沿 (1,1) 方向
    fix X[5][2] = {
        {fx::itofix(0), fx::itofix(0)},
        {fx::itofix(1), fx::itofix(1)},
        {fx::itofix(2), fx::itofix(2)},
        {fx::itofix(3), fx::itofix(3)},
        {fx::itofix(4), fx::itofix(4)},
    };
    fix flat[10];
    for (int i = 0; i < 5; i++) { flat[i*2] = X[i][0]; flat[i*2+1] = X[i][1]; }

    Pca pca;
    pca.fit(flat, 5, 2, 1);

    // 第一主成分方向应近似 (1,1)/sqrt2 ≈ (0.707, 0.707) 或反号
    fix c0 = pca.comps[0], c1 = pca.comps[1];
    // 绝对值都应接近 0.707
    fix a0 = c0 < 0 ? -c0 : c0;
    fix a1 = c1 < 0 ? -c1 : c1;
    fix target = fx::fxf(707, 1000);
    if (!fx_close(a0, target, fx::fxf(50, 1000))) fails++;
    if (!fx_close(a1, target, fx::fxf(50, 1000))) fails++;

    // 解释方差比：第一主成分应占绝大部分（>90%）
    fix r = pca.explained_ratio(0);
    if (r < fx::fxf(90, 100)) fails++;

    // transform：点 (4,4) 投影到第一主成分应得到较大正值（沿方向）
    fix q[2] = {fx::itofix(4), fx::itofix(4)};
    fix out[1];
    pca.transform(q, out);
    // 中心化后 q - mean(2,2) = (2,2)，投影到 (0.707,0.707) = 2*0.707*2 ≈ 2.83
    if (out[0] <= 0) fails++;

    return fails;
}

} // namespace ml
} // namespace nefu
