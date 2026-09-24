// nefuOS 机器学习库 —— 随机森林
// 多棵决策树集成：每棵树用自助采样（bootstrap）训练，分裂时随机挑选特征子集。
// 预测：分类多数表决；回归：平均。带 OOB（袋外）误差估计。
#pragma once
#include "decisiontree.h"

namespace nefu {
namespace ml {

struct RandomForest {
    int      n_trees;
    int      max_features;     // 每次分裂随机考虑的特征数（0 = 用全部）
    int      max_depth;
    int      n_classes;
    int      d;
    int      mf_used;          // 实际使用的特征子集宽度
    int*     feat_sub;         // n_trees * mf_used：每棵树选中的原始特征下标
    DTree*   trees;            // n_trees 棵
    bool     regression;       // true=回归森林，false=分类森林
    Rng      rng;

    RandomForest();
    ~RandomForest();
    void free_forest();
    void fit(const DataSet& ds, uint32_t seed);
    void fit_regression(const DataSet& ds, uint32_t seed);
    int  predict(const fix* x) const;
    fix  predict_regression(const fix* x) const;
    fix  accuracy(const DataSet& ds) const;
    fix  mse(const DataSet& ds) const;
};

// ---------------- 自检 ----------------
// 两簇明显可分的二维点，随机森林应高准确率分类。
int randomforest_self_test();

} // namespace ml
} // namespace nefu
