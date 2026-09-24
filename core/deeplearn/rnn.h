// nefuOS 深度学习库 —— 循环网络
// RNN / LSTM / GRU cell，序列前向。权重复用 tensor 算子，反向自动走磁带。
// 典型用法：
//   RNNCell cell(D, H);
//   Tensor h = cell.forward(x, h_prev);
//   BiRNN bi(D,H); bi.forward(seq, T);
//
// 前向公式（全定点）：
//   RNN:  h_t = tanh(Wxh @ x_t + Whh @ h_{t-1} + b)
//   LSTM: i=sigmoid(...), f=sigmoid(...), g=tanh(...), o=sigmoid(...)
//          c_t = f*c_{t-1} + i*g ; h_t = o*tanh(c_t)
//   GRU:  z=sigmoid, r=sigmoid, h~=tanh(Wx x + Wh(r*h))
//          h_t = (1-z)*h_{t-1} + z*h~
// 输入约定：x[D] 为 2D [1,D]，h[H] 为 2D [1,H]。
// cell 级融合前向不挂反向节点（BPTT 由高层展开循环实现）。
// 约定：输入序列 x[T, D]，隐藏状态 h[H]。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 简单 RNN cell：h' = tanh(Wxh @ x + Whh @ h + b)
struct RNNCell {
    int D, H;
    Tensor Wxh;   // [D,H]
    Tensor Whh;   // [H,H]
    Tensor b;     // [H]
    RNNCell(int in_dim, int hid_dim, uint32_t seed);
    // 单步：x[D] 行向量（1D Tensor），h[H]，返回新 h[H]
    Tensor forward(const Tensor& x, const Tensor& h);
    void params(List<Tensor*>& out);
};

// LSTM cell：i,f,g,o 门
struct LSTMCell {
    int D, H;
    Tensor Wx;    // [D,4H]
    Tensor Wh;    // [H,4H]
    Tensor b;     // [4H]
    LSTMCell(int in_dim, int hid_dim, uint32_t seed);
    // x[D], h[H], c[H] -> 返回新 h；新 c 写入 out_c（调用方提供缓冲）
    Tensor forward(const Tensor& x, const Tensor& h, const Tensor& c, Tensor& out_c);
    void params(List<Tensor*>& out);
};

// GRU cell
struct GRUCell {
    int D, H;
    Tensor Wx; Tensor Wh; Tensor b;
    GRUCell(int in_dim, int hid_dim, uint32_t seed);
    Tensor forward(const Tensor& x, const Tensor& h);
    void params(List<Tensor*>& out);
};

// 整段序列前向：x[T,D]，返回所有时间步隐藏 [T,H]（堆叠）
Tensor rnn_forward_sequence(RNNCell& cell, const Tensor& x_seq, const Tensor& h0);

int rnn_self_test();

} // namespace deeplearn
} // namespace nefu
