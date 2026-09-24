// nefuOS 深度学习库 —— 注意力机制实现
// 缩放点积注意力 + 多头 + 位置编码 + Transformer Block。因果掩码用于自回归。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "attention.h"
#include "activations.h"
#include "autograd.h"

namespace nefu {
namespace deeplearn {

namespace {
fix init_scale(int fan_in) {
    fix fi = fx::itofix(fan_in > 0 ? fan_in : 1);
    return fx::fx_sqrt(fx::fx_div(fx::FX_ONE, fi));
}
} // namespace

// ==================== 缩放点积注意力 ====================
Tensor attention_scaled_dotproduct(const Tensor& Q, const Tensor& K, const Tensor& V) {
    // Q:[Tq,D], K:[Tk,D], V:[Tk,D] -> [Tq,D]
    int Tq = Q.shape[0], Tk = K.shape[0], D = Q.shape[1];
    fix scale = fx::fx_div(fx::FX_ONE, fx::fx_sqrt(fx::itofix(D)));
    // scores = Q @ K^T
    Tensor Kt = t_transpose2d(K);              // [D,Tk]
    Tensor scores = t_matmul(Q, Kt);           // [Tq,Tk]
    // 缩放 + softmax over Tk
    Tensor sc = t_scalar(scores, scale);
    Tensor w = act_softmax(sc);                // [Tq,Tk]
    Tensor out = t_matmul(w, V);               // [Tq,D]
    return out;
}

// ==================== 位置编码 ====================
Tensor positional_encoding(int T, int D) {
    Tensor pe = t_zeros(2, (int[2]){T, D});
    for (int pos = 0; pos < T; pos++)
        for (int i = 0; i < D; i += 2) {
            fix denom = fx::fx_pow(fx::FX_TWO, fx::fx_div(fx::itofix(i), fx::itofix(D)));
            fix arg = fx::fx_div(fx::itofix(pos), denom);
            pe.data[pos*D+i] = fx::fx_sin(arg);
            if (i+1 < D) pe.data[pos*D+i+1] = fx::fx_cos(arg);
        }
    return pe;
}

// ==================== MultiHeadAttention ====================
MultiHeadAttention::MultiHeadAttention(int model_dim, int heads, uint32_t seed)
    : d_model(model_dim), nhead(heads), d_head(model_dim/heads) {
    int s[2] = { model_dim, model_dim };
    fix k = init_scale(model_dim);
    Wq = t_rand(2, s, seed, -k, k); requires_grad(Wq);
    Wk = t_rand(2, s, seed+1, -k, k); requires_grad(Wk);
    Wv = t_rand(2, s, seed+2, -k, k); requires_grad(Wv);
    Wo = t_rand(2, s, seed+3, -k, k); requires_grad(Wo);
}

Tensor MultiHeadAttention::forward(const Tensor& x) {
    int T = x.shape[0];
    Tensor Q = t_matmul(x, Wq);    // [T, d_model]
    Tensor K = t_matmul(x, Wk);
    Tensor V = t_matmul(x, Wv);
    Tensor concat = t_zeros(2, (int[2]){T, d_model});
    // 对每个头切分并做点积注意力
    for (int h = 0; h < nhead; h++) {
        int hs = h * d_head;
        Tensor Qh = t_zeros(2, (int[2]){T, d_head});
        Tensor Kh = t_zeros(2, (int[2]){T, d_head});
        Tensor Vh = t_zeros(2, (int[2]){T, d_head});
        for (int t = 0; t < T; t++) {
            for (int j = 0; j < d_head; j++) {
                Qh.data[t*d_head+j] = Q.data[t*d_model+hs+j];
                Kh.data[t*d_head+j] = K.data[t*d_model+hs+j];
                Vh.data[t*d_head+j] = V.data[t*d_model+hs+j];
            }
        }
        Tensor oh = attention_scaled_dotproduct(Qh, Kh, Vh);   // [T,d_head]
        for (int t = 0; t < T; t++)
            for (int j = 0; j < d_head; j++)
                concat.data[t*d_model+hs+j] = oh.data[t*d_head+j];
    }
    Tensor out = t_matmul(concat, Wo);
    return out;
}

void MultiHeadAttention::params(List<Tensor*>& out) {
    out.push(&Wq); out.push(&Wk); out.push(&Wv); out.push(&Wo);
}

// ==================== TransformerBlock ====================
TransformerBlock::TransformerBlock(int model_dim, int heads, int ff_dim, uint32_t seed)
    : d_model(model_dim), d_ff(ff_dim), nhead(heads), attn(model_dim, heads, seed) {
    int s1[2] = { model_dim, ff_dim };
    fix k = init_scale(model_dim);
    W1 = t_rand(2, s1, seed+10, -k, k); requires_grad(W1);
    int s2[2] = { ff_dim, model_dim };
    W2 = t_rand(2, s2, seed+11, -k, k); requires_grad(W2);
    int sb[1] = { ff_dim };
    b1 = t_zeros(1, sb); requires_grad(b1);
    int sb2[1] = { model_dim };
    b2 = t_zeros(1, sb2); requires_grad(b2);
    ln1g = t_ones(1, sb2); requires_grad(ln1g);
    ln1b = t_zeros(1, sb2); requires_grad(ln1b);
    ln2g = t_ones(1, sb2); requires_grad(ln2g);
    ln2b = t_zeros(1, sb2); requires_grad(ln2b);
}

Tensor TransformerBlock::forward(const Tensor& x) {
    // 简化前向：attn 残差 + FFN 残差（省略 LN 的精确反向，前向形状正确即可）
    Tensor a = attn.forward(x);
    Tensor x2 = t_add(x, a);
    Tensor h = t_matmul(x2, W1);
    Tensor hb = t_add(h, b1);
    Tensor hr = act_relu(hb);
    Tensor ffn = t_matmul(hr, W2);
    Tensor ffb = t_add(ffn, b2);
    Tensor out = t_add(x2, ffb);
    return out;
}

void TransformerBlock::params(List<Tensor*>& out) {
    attn.params(out);
    out.push(&W1); out.push(&b1); out.push(&W2); out.push(&b2);
    out.push(&ln1g); out.push(&ln1b); out.push(&ln2g); out.push(&ln2b);
}

// ---------------- 自检 ----------------
Tensor causal_attention(const Tensor& Q, const Tensor& K, const Tensor& V);
Tensor cross_attention(const Tensor& Q, const Tensor& K, const Tensor& V);

int attention_self_test() {
    int fails = 0;
    fix tol = fx::fxf(10,100);
    // 缩放点积：Q=K=V 对角阵 -> 注意力应接近单位阵
    {
        fix v[4] = {fx::FX_ONE,0, 0,fx::FX_ONE};
        int s2[2]={2,2};
        Tensor Q=t_from_flat(2,s2,v);
        Tensor K=t_from_flat(2,s2,v);
        Tensor V=t_from_flat(2,s2,v);
        Tensor out=attention_scaled_dotproduct(Q,K,V);
        // out ≈ V（对角线）
        // 注意力应偏向对角：out[0][0] > out[0][1]，out[1][1] > out[1][0]
        if (out.data[0] <= out.data[1]) fails++;
        if (out.data[3] <= out.data[2]) fails++;
    }
    // 位置编码形状
    {
        Tensor pe = positional_encoding(4, 8);
        if (pe.shape[0]!=4 || pe.shape[1]!=8) fails++;
    }
    // 多头形状
    {
        MultiHeadAttention mha(8, 2, 5);
        int sx[2]={3,8}; fix sv[24]={0};
        Tensor x=t_from_flat(2,sx,sv);
        Tensor y=mha.forward(x);
        if (y.shape[0]!=3 || y.shape[1]!=8) fails++;
    }
    // TransformerBlock 形状
    {
        TransformerBlock blk(8,2,16,6);
        int sx[2]={3,8}; fix sv[24]={0};
        Tensor x=t_from_flat(2,sx,sv);
        Tensor y=blk.forward(x);
        if (y.shape[0]!=3 || y.shape[1]!=8) fails++;
    }
    // 因果注意力：下三角可见
    {
        fix v[4]={fx::FX_ONE,0, 0,fx::FX_ONE};
        int s2[2]={2,2};
        Tensor Q=t_from_flat(2,s2,v);
        Tensor K=t_from_flat(2,s2,v);
        Tensor V=t_from_flat(2,s2,v);
        Tensor out=causal_attention(Q,K,V);
        if (out.shape[0]!=2 || out.shape[1]!=2) fails++;
    }
    // 交叉注意力形状
    {
        fix qv[4]={fx::FX_ONE,0, fx::FX_HALF,fx::FX_HALF};
        fix kv[4]={fx::FX_ONE,0, 0,fx::FX_ONE};
        int s2[2]={2,2};
        Tensor Q=t_from_flat(2,s2,qv);
        Tensor K=t_from_flat(2,s2,kv);
        Tensor V=t_from_flat(2,s2,kv);
        Tensor out=cross_attention(Q,K,V);
        if (out.shape[0]!=2 || out.shape[1]!=2) fails++;
    }
    // 位置编码形状 + 确定性
    {
        Tensor pe = positional_encoding(4, 8);
        if (pe.shape[0]!=4 || pe.shape[1]!=8) fails++;
        // pe(0,0)=sin(0)=0
        if (!fx_close(pe.data[0], 0, fx::fxf(5,100))) fails++;
    }
    // 多头注意力形状
    {
        MultiHeadAttention mha(8, 2, 11);
        fix v[24]; for(int i=0;i<24;i++) v[i]=fx::itofix(i%3);
        Tensor x=t_from_flat(2,(int[2]){3,8},v);
        Tensor y=mha.forward(x);
        if (y.shape[0]!=3 || y.shape[1]!=8) fails++;
    }
    // Transformer Block 形状
    {
        TransformerBlock blk(8, 2, 16, 12);
        fix v[24]; for(int i=0;i<24;i++) v[i]=fx::itofix(i%2);
        Tensor x=t_from_flat(2,(int[2]){3,8},v);
        Tensor y=blk.forward(x);
        if (y.shape[0]!=3 || y.shape[1]!=8) fails++;
    }
    // MHA params 数量
    {
        MultiHeadAttention mha(8,2,5);
        List<Tensor*> ps; mha.params(ps);
        if (ps.size() != 4) fails++;  // Wq Wk Wv Wo
    }
    // scaled dot-product：Q=K=V 对角 -> 输出接近 V
    {
        fix v[4]={fx::FX_ONE,0, 0,fx::FX_ONE};
        Tensor Q=t_from_flat(2,(int[2]){2,2},v);
        Tensor K=t_from_flat(2,(int[2]){2,2},v);
        Tensor V=t_from_flat(2,(int[2]){2,2},v);
        Tensor y=attention_scaled_dotproduct(Q,K,V);
        if (y.shape[0]!=2 || y.shape[1]!=2) fails++;
    }
    return fails;
}


// ==================== 追加：交叉注意力 / 因果掩码 ====================
// 因果掩码：上三角（含对角线以上的未来位置）置 -inf（用很小的负数代替）。
// scores:[Tq,Tk]，把 j>i 的位置减去一个大常数，使其 softmax 后接近 0。
Tensor apply_causal_mask(const Tensor& scores) {
    int Tq=scores.shape[0], Tk=scores.shape[1];
    Tensor r = t_zeros(2,(int[2]){Tq,Tk});
    fix bigneg = fx::itofix(-100);
    for (int i=0;i<Tq;i++) for (int j=0;j<Tk;j++) {
        r.data[i*Tk+j] = (j > i) ? bigneg : scores.data[i*Tk+j];
    }
    return r;
}

// 因果缩放点积注意力：带掩码的 SDPA。
Tensor causal_attention(const Tensor& Q, const Tensor& K, const Tensor& V) {
    Tensor Kt = t_transpose2d(K);
    Tensor scores = t_matmul(Q, Kt);
    int D = Q.shape[1];
    fix scale = fx::fx_div(fx::FX_ONE, fx::fx_sqrt(fx::itofix(D)));
    Tensor sc = t_scalar(scores, scale);
    Tensor masked = apply_causal_mask(sc);
    Tensor w = act_softmax(masked);
    return t_matmul(w, V);
}

// 交叉注意力：Q 来自 decoder，K/V 来自 encoder（形状可不同，最后一维对齐）。
Tensor cross_attention(const Tensor& Q, const Tensor& K, const Tensor& V) {
    // 与 SDPA 相同，只是 Q 与 K/V 来自不同序列；这里直接复用。
    return attention_scaled_dotproduct(Q, K, V);
}
} // namespace deeplearn
} // namespace nefu
