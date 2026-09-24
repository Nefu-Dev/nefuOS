// nefuOS 深度学习库 —— 聚合头
// 包含全部子模块，并提供 deeplearn_self_test() 汇总。
//
// ==================== 使用指南（bare-metal，全定点 Q16.16） ====================
//
// 1. 构造数据：用 t_from_flat 从 C 数组建张量，或 t_zeros/t_rand。
//    所有"浮点"值用 fx::itofix(n) 或 fx::fxf(a,b)（=a/b 的定点）。
//
// 2. 前向：调用 Dense/Conv2D/act_relu 等，结果自动挂反向节点。
//    叶子参数需 requires_grad(w)。中间张量必须命名，生命期覆盖 backward。
//
// 3. 反向：backward(loss标量) 沿 tape 反向传播梯度到各 w.grad。
//
// 4. 步进：opt.step() 用 w.grad 更新 w.data；opt.zero_grad() 清梯度。
//
// 5. 循环：tape_reset() -> 前向 -> backward -> step -> zero_grad -> tape_reset()。
//
// 示例（线性回归 y=wx+b）：
//   tape_reset();
//   Tensor X = t_from_flat(2,(int[2]){N,1}, xflat);
//   Tensor y = t_matmul(X, W) + b;   // W[1,1], b[1]
//   Tensor loss = loss_mse(y, T);
//   backward(loss); opt.step(); opt.zero_grad(); tape_reset();
//
// 注意：跨函数返回的中间张量会析构，其反向节点存的指针会悬空，
//       因此不要把 MLP::forward() 的返回张量直接拿去 backward；
//       中间张量必须在调用帧内命名并保持存活。
//
// 常见坑：
//   * 不要在子函数里 forward 完就 return Tensor，再在外层 backward；
//   * RNN/LSTM cell 的 x/h 必须是 2D [1,D]/[1,H]，1D 会在 matmul 死循环；
//   * 手写清零循环必须用 volatile 或文件级 pragma，否则 -O2 栈崩溃；
//   * ksprintf 不支持 %f，定点打印用整数除法手动拼接；
//   * 所有点积用 int64 累加后 >>16，否则 Q16.16 溢出。
// ============================================================================
#pragma once

#include "tensor.h"
#include "autograd.h"
#include "activations.h"
#include "losses.h"
#include "layers.h"
#include "optimizers.h"
#include "rnn.h"
#include "attention.h"
#include "models.h"
#include "data.h"
#include "trainer.h"
#include "weights.h"
#include "metrics.h"

namespace nefu {
namespace deeplearn {

// 运行全部子模块自检，返回失败总数（应为 0）。
inline int deeplearn_self_test() {
    int f = 0;
    f += tensor_self_test();
    f += autograd_self_test();
    f += activations_self_test();
    f += losses_self_test();
    f += layers_self_test();
    f += optimizers_self_test();
    f += rnn_self_test();
    f += attention_self_test();
    f += models_self_test();
    f += data_self_test();
    f += trainer_self_test();
    f += weights_self_test();
    f += metrics_self_test();
    return f;
}

} // namespace deeplearn
} // namespace nefu
