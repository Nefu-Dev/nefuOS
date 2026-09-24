// nefuOS 深度学习库 —— 注意力机制
// Scaled Dot-Product / Multi-Head Attention / Positional Encoding / Transformer Block。
// 典型用法：
//   MultiHeadAttention mha(d_model, nhead);
//   Tensor out = mha.forward(Q, K, V);
//
// 缩放点积：Attention(Q,K,V)=softmax(QK^T/sqrt(d_k))V
// 多头：把 d_model 分成 nhead 份，各自做注意力再拼接。
// 位置编码：sin/cos 固定位置向量，加入输入以携带顺序信息。
// Transformer Block：MHA + 残差 + LayerNorm + FFN + 残差 + LayerNorm。
#pragma once
#include "tensor.h"

namespace nefu {
namespace deeplearn {

// 缩放点积注意力：Q,K,V 均为 [T,D]（这里简化为单头）。
// 输出 [T,D]。缩放因子 1/sqrt(D)。
Tensor attention_scaled_dotproduct(const Tensor& Q, const Tensor& K, const Tensor& V);

// 位置编码（正弦）：长度 T，维度 D，返回 [T,D]（不加到模型参数）
Tensor positional_encoding(int T, int D);

// 多头注意力：d_model，nhead。x: [T, d_model]。
// 内部 QKV 投影 + 分头 + 拼接 + 输出投影。
struct MultiHeadAttention {
    int d_model, nhead, d_head;
    Tensor Wq, Wk, Wv, Wo;   // 都是 [d_model, d_model]
    MultiHeadAttention(int model_dim, int heads, uint32_t seed);
    Tensor forward(const Tensor& x);
    void params(List<Tensor*>& out);
};

// 简化 Transformer Block：LayerNorm + MHA + 残差 + FFN(两层 Dense 式)。
// 这里给出前向计算（权重内部持有）。
struct TransformerBlock {
    int d_model, d_ff, nhead;
    MultiHeadAttention attn;
    Tensor W1, b1, W2, b2;   // FFN: d_model -> d_ff -> d_model
    Tensor ln1g, ln1b, ln2g, ln2b;
    TransformerBlock(int model_dim, int heads, int ff_dim, uint32_t seed);
    Tensor forward(const Tensor& x);   // x:[T,d_model]
    void params(List<Tensor*>& out);
};

int attention_self_test();

} // namespace deeplearn
} // namespace nefu
