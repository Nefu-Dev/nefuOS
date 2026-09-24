// nefuOS 深度学习库 —— 激活函数（全定点 Q16.16）
//
// 全部定点实现：
//   ReLU:      max(0,x)
//   LeakyReLU: x>0 ? x : 0.01*x
//   ELU:       x>0 ? x : alpha*(e^x-1)
//   GELU:      x*sigmoid(1.702*x)（近似）
//   Sigmoid:   1/(1+e^-x)
//   Tanh:      (e^{2x}-1)/(e^{2x}+1)
//   Softmax:   每行 exp/sum exp（数值稳定减最大值）
//   Swish:     x*sigmoid(x)
//   Hardswish: x*relu6(x+3)/6
//   Mish:      x*tanh(softplus(x))
//   Softplus:  ln(1+e^x)
// 所有激活自动挂反向节点。
// 典型用法：Tensor y = act_relu(x);
// 每个激活返回新张量并注册反向节点。Sigmoid/Tanh 用 fx_exp，
// GELU/Swish 用近似。Softmax 沿最后一维归一。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 逐元素激活（输出形状与输入相同）
Tensor act_relu(const Tensor& x);            // max(0,x)
Tensor act_leaky_relu(const Tensor& x, fix negative_slope = fx::fxf(1,100));
Tensor act_elu(const Tensor& x, fix alpha = fx::FX_ONE);
Tensor act_sigmoid(const Tensor& x);          // 1/(1+e^-x)
Tensor act_tanh(const Tensor& x);             // (e^{2x}-1)/(e^{2x}+1)
Tensor act_swish(const Tensor& x);            // x * sigmoid(x)
// GELU 近似：x * sigmoid(1.702*x)
Tensor act_gelu(const Tensor& x);
// Hardswish：x * relu6(x+3)/6
Tensor act_hardswish(const Tensor& x);
// Mish：x * tanh(ln(1+e^x))
Tensor act_mish(const Tensor& x);
// Softplus：ln(1+e^x)（平滑 ReLU）
Tensor act_softplus(const Tensor& x);
// Softmax 沿最后一维（每行和为 1）。保留形状。
Tensor act_softmax(const Tensor& x);

// ---------------- 自检 ----------------
int activations_self_test();

} // namespace deeplearn
} // namespace nefu
