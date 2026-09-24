// nefuOS 机器学习库 —— 主成分分析（PCA）
// 步骤：数据中心化 -> 协方差矩阵 -> Jacobi 特征值分解 -> 投影到前 k 主成分。
// 输出：降维后向量（k 维），并给出各主成分解释方差比。
#pragma once
#include "matrix.h"

namespace nefu {
namespace ml {

struct Pca {
    int   d;           // 原始维数
    int   k;           // 降维后维数
    fix*  mean;        // 每维均值（长度 d）
    fix*  comps;       // d*k：前 k 个主成分方向（按特征值降序），行主序
    fix*  eigvals;     // d：全部特征值
    Pca();
    ~Pca();
    void release();
    void fit(const fix* Xflat, int n, int d_, int k_);
    // 把一个 d 维原始向量投影到 k 维主成分空间，写入 out（长度 k）。
    void transform(const fix* x, fix* out) const;
    // 逆变换：把 k 维主成分坐标还原回 d 维原始空间（加回均值），写入 out（长度 d）。
    void inverse_transform(const fix* z, fix* out) const;
    // 白化：投影后再除以 sqrt(特征值)，使各主成分方差为 1。out 长度 k。
    void whiten(const fix* x, fix* out) const;
    // 第 i 个主成分的解释方差比 [0,1]
    fix explained_ratio(int i) const;
};

// ---------------- 自检 ----------------
// 沿 y≈x 方向拉长的二维点，PCA 第一主成分应接近 (1,1)/sqrt2，
// 降维到 1 维后能保留主要方差。
int pca_self_test();

} // namespace ml
} // namespace nefu
