// nefuOS 机器学习库 —— 朴素贝叶斯分类
// 高斯模型：连续特征，按类估计均值/方差，预测用对数高斯似然。
// 多项式模型：计数特征（如词袋），带拉普拉斯平滑。
// 伯努利模型：二值特征（0/1 出现与否）。
// 全 Q16.16；预测走对数概率避免下溢。
#pragma once
#include "dataset.h"

namespace nefu {
namespace ml {

// ---------------- 高斯朴素贝叶斯 ----------------
struct GaussianNB {
    int   classes;
    int   d;
    fix*  mean;     // classes*d
    fix*  var;      // classes*d
    fix*  logprior; // classes
    GaussianNB();
    ~GaussianNB();
    void release();
    void fit(const fix* Xflat, const int* yc, int n);
    int  predict(const fix* x) const;
    fix  accuracy(const fix* Xflat, const int* yc, int n) const;
};

// ---------------- 多项式朴素贝叶斯（计数特征） ----------------
struct MultinomialNB {
    int   classes;
    int   d;
    fix*  logprob;  // classes*d：P(x_j=1|c) 的对数
    fix*  logprior; // classes
    fix   alpha;    // 拉普拉斯平滑
    MultinomialNB();
    ~MultinomialNB();
    void release();
    void fit(const fix* Xcount, const int* yc, int n);   // Xcount 为整数计数（存为 fx）
    int  predict(const fix* xcount) const;
};

// ---------------- 伯努利朴素贝叶斯（0/1 特征） ----------------
struct BernoulliNB {
    int   classes;
    int   d;
    fix*  logprob;  // classes*d：P(x_j=1|c)
    fix*  logprob_neg; // classes*d：P(x_j=0|c) = 1-P(x_j=1|c)
    fix*  logprior;
    BernoulliNB();
    ~BernoulliNB();
    void release();
    void fit(const fix* Xbin, const int* yc, int n);     // Xbin 每维 0 或 1
    int  predict(const fix* xbin) const;
};

// ---------------- 自检 ----------------
// 高斯 NB：两个高斯簇（类0 均值(0,0)，类1 均值(5,5)）应正确分类。
int bayes_self_test();

} // namespace ml
} // namespace nefu
