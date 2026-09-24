// nefuOS 深度学习库 —— 层实现
// Dense / Conv2D / MaxPool2D / AvgPool2D / Flatten / Dropout /
// BatchNorm / LayerNorm / Embedding。
// 层是持有参数 Tensor 的结构体；forward 复用 tensor/activations 的反向节点。
// 参数通过 params() 收集，交给优化器更新。
#pragma once
#include "tensor.h"
#include "activations.h"

namespace nefu {
namespace deeplearn {

// ---------------- 全连接层 ----------------
// y = x @ W + b。x: [N,in]，W: [in,out]，b: [out]，y: [N,out]
struct Dense {
    int in_dim, out_dim;
    Tensor W;
    Tensor b;
    Dense() {}
    Dense(int in_d, int out_d, uint32_t seed);
    Tensor forward(const Tensor& x);
    void params(List<Tensor*>& out);
};

// ---------------- 二维卷积（stride=1, pad=0） ----------------
// x: [N,Ci,H,W]；W: [K,Ci,R,S]；b: [K]；y: [N,K,OH,OW]
struct Conv2D {
    int N, Ci, H, Wd;       // 输入形状（forward 时记录）
    int K, R, S;            // 卷积核
    int OH, OW;
    Tensor kernel;          // [K,Ci,R,S]
    Tensor bias;            // [K]
    Conv2D() {}
    Conv2D(int out_ch, int in_ch, int kr, int kc, uint32_t seed);
    Tensor forward(const Tensor& x);
    void params(List<Tensor*>& out);
};

// ---------------- 2x2 池化（stride=2） ----------------
struct MaxPool2D {
    Tensor forward(const Tensor& x);   // x:[N,C,H,W] -> [N,C,H/2,W/2]
};
struct AvgPool2D {
    Tensor forward(const Tensor& x);
};

// ---------------- Flatten：[N,...] -> [N, prod(...)] ----------------
Tensor layer_flatten(const Tensor& x);

// ---------------- Dropout（训练时随机置零，推理恒等） ----------------
struct Dropout {
    fix p;                 // 丢弃概率 0..1
    bool training;
    Dropout(fix prob = fx::fxf(5,10), bool train = true) : p(prob), training(train) {}
    Tensor forward(const Tensor& x, uint32_t seed);
};

// ---------------- BatchNorm（对 N,H,W 维归一，沿通道） ----------------
// x: [N,C,H,W]；对每个通道 c 计算均值/方差。训练模式前向，推理用累积统计。
struct BatchNorm2D {
    int C;
    Tensor gamma;          // [C]
    Tensor beta;           // [C]
    fix running_mean[64];
    fix running_var[64];
    fix momentum;
    bool training;
    BatchNorm2D(int channels, bool train = true);
    Tensor forward(const Tensor& x);
    void params(List<Tensor*>& out);
};

// ---------------- LayerNorm（沿最后一维归一） ----------------
struct LayerNorm {
    int D;
    Tensor gamma; Tensor beta;
    LayerNorm(int dim);
    Tensor forward(const Tensor& x);   // x: [..., D]
    void params(List<Tensor*>& out);
};

// ---------------- Embedding ----------------
struct Embedding {
    int V, D;
    Tensor table;          // [V,D]
    Embedding(int vocab, int dim, uint32_t seed);
    Tensor forward(const int* ids, int n);   // ids 长度 n -> [n,D]
    void params(List<Tensor*>& out);
};

// ---------------- 自检 ----------------
int layers_self_test();

} // namespace deeplearn
} // namespace nefu
