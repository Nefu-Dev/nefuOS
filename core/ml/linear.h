// nefuOS 机器学习库 —— 线性模型
// 线性回归：批量梯度下降 / 随机梯度下降 / 正规方程（最小二乘），带 L2 正则。
// 逻辑回归：sigmoid + 交叉熵，批量梯度下降；支持二分类概率输出与多分类 OvR。
// 全部 Q16.16 定点实现。
#pragma once
#include "matrix.h"
#include "dataset.h"

namespace nefu {
namespace ml {

// ---------------- 线性回归 ----------------
struct LinearReg {
    int   d;
    fix*  w;        // 权重，长度 d
    fix   b;        // 偏置
    LinearReg();
    ~LinearReg();
    void init(int d_);
    void release();
    fix predict(const fix* x) const;
    // 批量梯度下降：迭代 iters 轮，学习率 lr（Q16.16），l2 正则系数。
    void fit_gd(const fix* Xflat, const fix* y, int n, int d_,
                fix lr, int iters, fix l2 = 0);
    // 随机梯度下降：每样本更新一次，shuffle 后逐轮。
    void fit_sgd(const fix* Xflat, const fix* y, int n, int d_,
                 fix lr, int epochs, uint32_t seed);
    // 正规方程：解析解 w = (X^T X)^-1 X^T y（X 自动加偏置列）。返回是否成功。
    bool fit_normal_eq(const fix* Xflat, const fix* y, int n, int d_);
    // 均方误差
    fix mse(const fix* Xflat, const fix* y, int n) const;
    // R² 决定系数（1 - SSE/SST）
    fix r2_score(const fix* Xflat, const fix* y, int n) const;
    // 平均绝对误差
    fix mae(const fix* Xflat, const fix* y, int n) const;
    // Ridge 正规方程：w = (X^T X + λI)^-1 X^T y（λ=l2，不含偏置正则）
    bool fit_ridge(const fix* Xflat, const fix* y, int n, int d_, fix l2);
};

// ---------------- 逻辑回归（二分类） ----------------
struct LogisticReg {
    int   d;
    fix*  w;
    fix   b;
    LogisticReg();
    ~LogisticReg();
    void init(int d_);
    void release();
    fix predict_prob(const fix* x) const;       // sigmoid(w·x+b)
    int  predict_class(const fix* x) const;       // prob>=0.5 ? 1 : 0
    // 交叉熵梯度下降。labels 为 0/1。
    void fit_gd(const fix* Xflat, const int* y, int n, int d_,
                fix lr, int iters, fix l2 = 0);
    // 预测准确率（0..1 Q16.16）
    fix accuracy(const fix* Xflat, const int* y, int n) const;
};

// ---------------- 多分类逻辑回归（OvR：一对多） ----------------
struct LogisticOvR {
    int   classes;
    int   d;
    LogisticReg* models;     // 每类一个二分类器
    LogisticOvR();
    ~LogisticOvR();
    void init(int classes_, int d_);
    void release();
    void fit(const fix* Xflat, const int* y, int n, fix lr, int iters);
    int  predict(const fix* x) const;           // 取概率最大的类
};

// ---------------- 自检 ----------------
// 线性回归拟合 y=2x+1（单变量，带噪声/不带噪声）-> w≈2, b≈1
// 逻辑回归线性可分两类 -> 训练集准确率接近 1
int linear_self_test();

// ---------------- 多项式特征展开（单变量） ----------------
// 把标量 x 展开为 [1, x, x^2, ..., x^(degree-1)]，写入 out（长度 degree）。
void poly_expand_univariate(fix x, int degree, fix* out);

// ---------------- Softmax 多分类逻辑回归（直接建模多类） ----------------
struct SoftmaxReg {
    int   d;
    int   classes;
    fix*  W;       // classes*d 权重
    fix*  B;       // classes 偏置
    SoftmaxReg();
    ~SoftmaxReg();
    void release();
    void init(int d_, int classes_);
    void predict_proba(const fix* x, fix* out) const;   // out 长度 classes
    int  predict(const fix* x) const;
    void fit(const fix* Xflat, const int* yc, int n, fix lr, int iters);
    fix  accuracy(const fix* Xflat, const int* yc, int n) const;
};

} // namespace ml
} // namespace nefu
