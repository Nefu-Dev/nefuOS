// nefuOS 机器学习库 —— K 近邻
// 分类：多数表决（可选距离加权）；回归：k 近邻标签均值。
// 距离度量：欧氏 / 曼哈顿 / 余弦。惰性学习，训练只存数据。
#pragma once
#include "dataset.h"

namespace nefu {
namespace ml {

enum DistMetric {
    DIST_EUCLIDEAN = 0,
    DIST_MANHATTAN = 1,
    DIST_COSINE    = 2
};

struct KnnModel {
    int   n;          // 训练样本数
    int   d;          // 维数
    const fix* X;     // 指向外部数据（不持有所有权）
    const int* yc;    // 分类标签（外部）
    DistMetric metric;
    bool  weighted;   // 距离加权投票

    KnnModel();
    void bind(const fix* X_, const int* yc_, int n_, int d_);
    fix distance(const fix* a, const fix* b) const;
    // 分类：返回预测类别。k 近邻多数表决。
    int  classify(const fix* x, int k, int classes) const;
    // 回归：返回 k 近邻标签均值。
    fix  regress(const fix* x, int k, const fix* yr) const;
    // 准确率
    fix  accuracy(const fix* Xt, const int* yt, int nt, int k, int classes) const;
};

// ---------------- 自检 ----------------
// 两类明显分离的二维点：类0 在 (-,-)，类1 在 (+,+)，KNN 应正确分类。
int knn_self_test();

} // namespace ml
} // namespace nefu
