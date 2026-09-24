// nefuOS 机器学习库 —— 前馈神经网络（全连接 MLP）
// 架构：输入层 -> 若干隐藏层(ReLU) -> 输出层(softmax 分类)。
// 训练：交叉熵损失 + 全批量梯度下降（反向传播）。带 L2 正则。
// 全 Q16.16 定点；权值用 new[] 分配。
#pragma once
#include "dataset.h"

namespace nefu {
namespace ml {

struct NeuralNet {
    int   n_layers;       // 层数（含输入与输出）
    int*  sizes;          // [d, h1, ..., out]，长度 n_layers
    fix** W;              // W[l] 长度 sizes[l]*sizes[l-1]，l=1..n_layers-1
    fix** B;              // B[l] 长度 sizes[l]
    // 前向缓存（训练时用）
    fix** a;              // a[l] 长度 sizes[l]
    fix** z;              // z[l] 长度 sizes[l]（隐藏层预激活）
    bool  trained;

    NeuralNet();
    ~NeuralNet();
    void release();
    // sizes_ 长度 L，拷贝副本。
    void init(const int* sizes_, int L);
    // 训练：Xflat[n*d], yc[n]。lr 学习率，iters 迭代轮数。
    void fit(const fix* Xflat, const int* yc, int n, int classes,
             fix lr, int iters);
    // 带动量 SGD：v = momentum*v - lr*grad；W += v。momentum 典型 0.9。
    void fit_momentum(const fix* Xflat, const int* yc, int n, int classes,
                      fix lr, int iters, fix momentum);
    void forward(const fix* x);                 // 计算 a[n_layers-1]
    int  predict(const fix* x);                  // argmax 输出
    fix  accuracy(const fix* Xflat, const int* yc, int n);
};

// ---------------- 自检 ----------------
// 线性可分的两类二维点（如 x+y>0），小型 MLP 应收敛到 100% 训练准确率。
int neuralnet_self_test();

} // namespace ml
} // namespace nefu
