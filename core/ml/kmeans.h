// nefuOS 机器学习库 —— K-Means 聚类
// K-Means++ 初始化（加速收敛、避免坏初始化），迭代分配-更新直到质心稳定。
// 评估：惯性（inertia，簇内平方距离和）与轮廓系数（silhouette）。
#pragma once
#include "dataset.h"

namespace nefu {
namespace ml {

struct KMeans {
    int   k;
    int   d;
    fix*  centers;     // k*d，行主序
    int*  labels;      // n 个样本所属簇（fit 后填）
    int   max_iter;

    KMeans();
    ~KMeans();
    void free_centers();
    void fit(const fix* Xflat, int n, int d_, int k_, uint32_t seed);
    // 多重启：尝试 restarts 次不同随机初始化，保留 inertia 最小的模型
    void fit_best(const fix* Xflat, int n, int d_, int k_, int restarts, uint32_t seed);
    int  predict(const fix* x) const;
    fix  inertia(const fix* Xflat, int n) const;       // 簇内平方距离和
    fix  silhouette(const fix* Xflat, int n) const;    // 平均轮廓系数 [-1,1]
    fix  davies_bouldin(const fix* Xflat, int n) const; // DBI 越小越好
};

// ---------------- 自检 ----------------
// 两个明显分离的二维簇，K-Means 应把它们分开，且 inertia 小、簇心接近真值。
int kmeans_self_test();

// ---------------- K-Medoids（基于原型点的聚类） ----------------
struct KMedoids {
    int   k;
    int   d;
    int*  medoids;     // k 个 medoid 在数据集中的下标
    int*  labels;
    KMedoids();
    ~KMedoids();
    void fit(const fix* Xflat, int n, int d_, int k_, int iters, uint32_t seed);
    fix total_cost(const fix* Xflat, int n) const;   // 簇内绝对距离和
};
int kmedoids_self_test();

} // namespace ml
} // namespace nefu
