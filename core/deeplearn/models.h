// nefuOS 深度学习库 —— 高阶模型包装
// Sequential MLP / 简单卷积分类器 / 回归头。
// 复用 Dense/Conv2D/Pool/激活，统一 forward + params + save/load 占位。
#pragma once
#include "tensor.h"
#include "layers.h"

namespace nefu {
namespace deeplearn {

// 多层感知机：input -> h1 -> h2 -> ... -> out，每层后接激活。
struct MLP {
    int n_layers;
    Dense** layers;       // 动态数组，长度 n_layers
    int*    act_types;    // 每层激活：0=relu 1=tanh 2=none
    int in_dim;
    MLP(const int* sizes, int n, uint32_t seed, int hidden_act = 0);
    ~MLP();
    Tensor forward(const Tensor& x);   // x:[B,in] -> [B,out]
    void params(List<Tensor*>& out);
};

// 简单卷积分类器：Conv2d->relu->maxpool->flatten->dense->out。
// 输入单通道 [B,1,H,W]，输出 [B, classes]。
struct TinyCNN {
    Conv2D conv;
    MaxPool2D pool;
    Dense  fc;
    int in_h, in_w, classes;
    TinyCNN(int H, int W, int classes, uint32_t seed);
    Tensor forward(const Tensor& x);
    void params(List<Tensor*>& out);
};

// 训练一个 MLP 做 XOR 分类，返回最终损失（用于自测/演示）。
fix mlp_xor_demo(uint32_t seed, int epochs);

int models_self_test();

} // namespace deeplearn
} // namespace nefu
