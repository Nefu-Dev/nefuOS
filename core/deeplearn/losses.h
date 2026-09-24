// nefuOS 深度学习库 —— 损失函数（全定点）
//
// 所有损失返回标量张量（size=1），可直接 backward。
// 典型用法：Tensor l = loss_mse(pred, target); backward(l);
// MSE:     mean((pred-target)^2)
// BCE:     sigmoid 交叉熵（数值稳定）
// Huber:   二次/线性混合，抗异常值
// CE:      softmax + NLL，梯度为 (softmax-onehot)/N
// NLL:     已有 log_softmax 上的 -lp[label]
// KL:      target*(log(target)-log_p)
// Hinge:   多类 SVM margin loss
// Contrastive: 正样本对拉近，负样本对推远
// MSE / BCE / Huber / CrossEntropy(带标签) / Contrastive。
// 所有损失返回标量张量（size=1），可直接 backward。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// MSE：mean((pred-target)^2)。pred/target 同形状。
Tensor loss_mse(const Tensor& pred, const Tensor& target);
// BCE：mean(-[t*log(p)+(1-t)*log(1-p)])，p 为 sigmoid(pred)（数值稳定形式）。
Tensor loss_bce(const Tensor& pred, const Tensor& target);
// Huber：|d|<delta ? 0.5*d^2 : delta*(|d|-0.5*delta)
Tensor loss_huber(const Tensor& pred, const Tensor& target, fix delta = fx::fxf(1,1));
// CrossEntropy：pred [N,C] logits，labels 长度 N 的类别下标。
// 返回平均 NLL（标量）。反向 dL/dpred = (softmax - onehot)/N。
Tensor loss_cross_entropy(const Tensor& pred, const int* labels, int N);
// Contrastive：对正样本对拉近、负样本对推远。
// a,b 为 [N,D] 嵌入；match[N] 1=正样本 0=负样本。margin 默认 1。
Tensor loss_contrastive(const Tensor& a, const Tensor& b, const uint8_t* match, int N,
                       fix margin = fx::FX_ONE);
// NLL：log_probs [N,C]（已经过 log_softmax），labels 长度 N 类别下标 -> 平均 -lp[label]
Tensor loss_nll(const Tensor& log_probs, const int* labels, int N);
// KL散度：mean( target * (log(target) - log_p) )，二者同形状且为概率分布
Tensor loss_kl_div(const Tensor& log_p, const Tensor& target);
// Hinge（多类 SVM）：mean over n of (max_{j!=y}(0, margin + s_j - s_y))
Tensor loss_hinge(const Tensor& scores, const int* labels, int N, fix margin = fx::FX_ONE);

int losses_self_test();

} // namespace deeplearn
} // namespace nefu
