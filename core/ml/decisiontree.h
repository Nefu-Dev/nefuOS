// nefuOS 机器学习库 —— 决策树分类
// 支持三种分裂准则：ID3 信息增益、C4.5 增益率、CART 基尼系数。
// 连续特征二分裂（按阈值：<=thr 走左，>thr 走右）。
// 预剪枝：最大深度 / 最小分裂样本数 / 节点纯度阈值。
// 全 Q16.16 定点；树用 new 节点，递归释放。
#pragma once
#include "dataset.h"

namespace nefu {
namespace ml {

enum Criterion {
    CRIT_ENTROPY = 0,   // ID3
    CRIT_GAINRATIO = 1, // C4.5
    CRIT_GINI = 2       // CART
};

struct DTreeNode {
    bool      is_leaf;
    int       class_label;   // 叶节点预测类
    fix       reg_value;     // 回归树叶节点预测值
    int       feat;          // 内部节点：分裂特征
    fix       threshold;     // 内部节点：分裂阈值
    DTreeNode* left;
    DTreeNode* right;
    DTreeNode();
    ~DTreeNode();
};

struct DTree {
    int        max_depth;
    int        min_samples_split;
    Criterion  crit;
    bool       regression;   // true=回归树（MSE 分裂），false=分类树
    DTreeNode* root;
    DTree();
    ~DTree();
    void free_tree();
    // 在数据集上训练分类树（只使用 idx 指向的样本）。
    void fit(const DataSet& ds, const int* idx, int n_idx);
    // 在数据集上训练回归树（ds.y 视为连续目标值）。
    void fit_regression(const DataSet& ds, const int* idx, int n_idx);
    int  predict(const fix* x) const;
    fix  predict_regression(const fix* x) const;
    fix  accuracy(const DataSet& ds) const;
    // 回归：均方误差
    fix  mse(const DataSet& ds) const;
};

// ---------------- 自检 ----------------
// 两个轴可分的二维簇（如 x<0.5 一类，x>0.5 另一类；或嵌套方块），
// 决策树应能在训练集上达到高准确率。
int decisiontree_self_test();

} // namespace ml
} // namespace nefu
