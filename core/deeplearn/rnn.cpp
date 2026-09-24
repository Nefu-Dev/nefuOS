// nefuOS 深度学习库 —— 循环网络实现
// RNN/LSTM/GRU cell 融合前向（不挂反向节点，BPTT 由高层展开）。BiRNN 双向拼接。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "rnn.h"
#include "autograd.h"
#include "activations.h"

namespace nefu {
namespace deeplearn {

namespace {
fix init_scale(int fan_in) {
    fix fi = fx::itofix(fan_in > 0 ? fan_in : 1);
    return fx::fx_sqrt(fx::fx_div(fx::FX_ONE, fi));
}
} // namespace

// ==================== RNNCell ====================
RNNCell::RNNCell(int in_dim, int hid_dim, uint32_t seed) : D(in_dim), H(hid_dim) {
    int s1[2] = { in_dim, hid_dim };
    fix k = init_scale(in_dim + hid_dim);
    Wxh = t_rand(2, s1, seed, -k, k); requires_grad(Wxh);
    int s2[2] = { hid_dim, hid_dim };
    Whh = t_rand(2, s2, seed+1, -k, k); requires_grad(Whh);
    int s3[1] = { hid_dim };
    b = t_zeros(1, s3); requires_grad(b);
}

Tensor RNNCell::forward(const Tensor& x, const Tensor& h) {
    // x:[1,D], h:[1,H]
    Tensor a = t_matmul(x, Wxh);     // [1,H]
    Tensor bm = t_matmul(h, Whh);    // [1,H]
    Tensor z = t_add(a, bm);         // [1,H]
    Tensor zb = t_add(z, b);         // bias 广播
    Tensor hn = act_tanh(zb);
    return hn;
}

void RNNCell::params(List<Tensor*>& out) {
    out.push(&Wxh); out.push(&Whh); out.push(&b);
}

// ==================== LSTMCell ====================
LSTMCell::LSTMCell(int in_dim, int hid_dim, uint32_t seed) : D(in_dim), H(hid_dim) {
    int s1[2] = { in_dim, 4*hid_dim };
    fix k = init_scale(in_dim + hid_dim);
    Wx = t_rand(2, s1, seed, -k, k); requires_grad(Wx);
    int s2[2] = { hid_dim, 4*hid_dim };
    Wh = t_rand(2, s2, seed+1, -k, k); requires_grad(Wh);
    int s3[1] = { 4*hid_dim };
    b = t_zeros(1, s3); requires_grad(b);
}

Tensor LSTMCell::forward(const Tensor& x, const Tensor& h, const Tensor& c, Tensor& out_c) {
    Tensor a = t_matmul(x, Wx);     // [1,4H]
    Tensor bm = t_matmul(h, Wh);    // [1,4H]
    Tensor z = t_add(a, bm);
    Tensor zb = t_add(z, b);
    // 门拆分（手工按段取，避免额外节点）
    int Hh = H;
    // i = sigmoid(zb[0:H]), f = sigmoid(zb[H:2H]), g = tanh(zb[2H:3H]), o = sigmoid(zb[3H:4H])
    // 为简化，这里直接在整块上算门并写回 out_c / h
    // out_c = f*c + i*g ；out_h = o*tanh(out_c)
    // 由于我们的张量库切片能力有限，这里用逐元素循环做一次"融合"前向。
    Tensor hn = t_zeros(2, (int[2]){1, H});
    // out_c 是调用方提供的普通缓冲 Tensor（[1,H]），这里直接写
    for (int i = 0; i < Hh; i++) {
        fix zi = zb.data[i];
        fix zf = zb.data[Hh+i];
        fix zg = zb.data[2*Hh+i];
        fix zo = zb.data[3*Hh+i];
        fix ig = fx::fx_div(fx::FX_ONE, fx::FX_ONE + fx::fx_exp(-zi));
        fix fg = fx::fx_div(fx::FX_ONE, fx::FX_ONE + fx::fx_exp(-zf));
        fix og = fx::fx_div(fx::FX_ONE, fx::FX_ONE + fx::fx_exp(-zo));
        fix twoxg = (fix)((fix64)zg << 1);
        fix eg = fx::fx_exp(twoxg);
        fix gg = fx::fx_div(eg - fx::FX_ONE, eg + fx::FX_ONE);
        fix cn = fx::fx_mul(fg, c.data[i]) + fx::fx_mul(ig, gg);
        out_c.data[i] = cn;
        fix twocn = (fix)((fix64)cn << 1);
        fix ec = fx::fx_exp(twocn);
        fix tcn = fx::fx_div(ec - fx::FX_ONE, ec + fx::FX_ONE);
        hn.data[i] = fx::fx_mul(og, tcn);
    }
    // 注意：融合前向未注册反向节点（cell 级 BPTT 留给高层），这里保证前向正确。
    return hn;
}

void LSTMCell::params(List<Tensor*>& out) {
    out.push(&Wx); out.push(&Wh); out.push(&b);
}

// ==================== GRUCell ====================
GRUCell::GRUCell(int in_dim, int hid_dim, uint32_t seed) : D(in_dim), H(hid_dim) {
    int s1[2] = { in_dim, 2*hid_dim };
    fix k = init_scale(in_dim + hid_dim);
    Wx = t_rand(2, s1, seed, -k, k); requires_grad(Wx);
    int s2[2] = { hid_dim, 2*hid_dim };
    Wh = t_rand(2, s2, seed+1, -k, k); requires_grad(Wh);
    int s3[1] = { 2*hid_dim };
    b = t_zeros(1, s3); requires_grad(b);
}

Tensor GRUCell::forward(const Tensor& x, const Tensor& h) {
    // r = sigmoid(Wx_r x + Wh_r h) ；z = sigmoid(...) ；n = tanh(Wx_n x + Wh_n (r*h))
    Tensor a = t_matmul(x, Wx);
    Tensor bm = t_matmul(h, Wh);
    Tensor z = t_add(t_add(a, bm), b);
    Tensor hn = t_zeros(2, (int[2]){1, H});
    for (int i = 0; i < H; i++) {
        fix zr = z.data[i], zz = z.data[H+i];
        fix r = fx::fx_div(fx::FX_ONE, fx::FX_ONE + fx::fx_exp(-zr));
        fix zg = fx::fx_div(fx::FX_ONE, fx::FX_ONE + fx::fx_exp(-zz));
        // n = tanh(xW_n + r*(h Wh_n))  -- 近似直接用 r 缩放 h 再算
        fix hr = fx::fx_mul(r, h.data[i]);
        fix two = (fix)((fix64)(hr + z.data[2*H+i]) << 1);
        fix e = fx::fx_exp(two);
        fix n = fx::fx_div(e - fx::FX_ONE, e + fx::FX_ONE);
        hn.data[i] = fx::fx_mul(fx::FX_ONE - zg, n) + fx::fx_mul(zg, h.data[i]);
    }
    return hn;
}

void GRUCell::params(List<Tensor*>& out) {
    out.push(&Wx); out.push(&Wh); out.push(&b);
}

// ==================== 序列前向 ====================
Tensor rnn_forward_sequence(RNNCell& cell, const Tensor& x_seq, const Tensor& h0) {
    int T = x_seq.shape[0], D = x_seq.shape[1], H = h0.shape[1];
    Tensor out = t_zeros(2, (int[2]){T, H});
    Tensor h = t_copy(h0);
    for (int t = 0; t < T; t++) {
        // 取 x_seq[t, :] 作为 [1,D]
        Tensor xt = t_zeros(2, (int[2]){1, D});
        for (int j = 0; j < D; j++) xt.data[j] = x_seq.data[t*D+j];
        h = cell.forward(xt, h);
        for (int j = 0; j < H; j++) out.data[t*H+j] = h.data[j];
    }
    return out;
}

// ---------------- 自检 ----------------
// ==================== 双向 RNN ====================
// 前向 cell + 反向 cell，输出拼接（2H 维）。
struct BiRNN {
    RNNCell fwd;
    RNNCell bwd;
    int D, H;
    BiRNN(int in_dim, int hid_dim, uint32_t seed)
        : fwd(in_dim, hid_dim, seed), bwd(in_dim, hid_dim, seed+1),
          D(in_dim), H(hid_dim) {}
    // x_seq:[T,D] -> out:[T,2H]
    Tensor forward(const Tensor& x_seq) {
        int T = x_seq.shape[0];
        Tensor hf = t_zeros(2,(int[2]){1,H});
        Tensor hb = t_zeros(2,(int[2]){1,H});
        Tensor out = t_zeros(2,(int[2]){T, 2*H});
        for (int t=0;t<T;t++) {
            Tensor xt = t_zeros(2,(int[2]){1,D});
            for (int j=0;j<D;j++) xt.data[j]=x_seq.data[t*D+j];
            hf = fwd.forward(xt, hf);
            for (int j=0;j<H;j++) out.data[t*2*H+j]=hf.data[j];
        }
        for (int t=T-1;t>=0;t--) {
            Tensor xt = t_zeros(2,(int[2]){1,D});
            for (int j=0;j<D;j++) xt.data[j]=x_seq.data[t*D+j];
            hb = bwd.forward(xt, hb);
            for (int j=0;j<H;j++) out.data[t*2*H+H+j]=hb.data[j];
        }
        return out;
    }
    void params(List<Tensor*>& out) {
        fwd.params(out); bwd.params(out);
    }
};

int rnn_self_test() {
    int fails = 0;
    // RNN 单步形状
    {
        RNNCell cell(3, 4, 1);
        int sx[2] = {1,3}; fix xv[3] = {fx::FX_ONE,0,fx::FX_HALF};
        Tensor x = t_from_flat(2,sx,xv);
        int sh[2] = {1,4}; fix hv[4] = {0,0,0,0};
        Tensor h = t_from_flat(2,sh,hv);
        Tensor hn = cell.forward(x,h);
        if (hn.shape[1] != 4) fails++;
        // tanh(0)=0，初始 h=0 但 W 非零，输出应非全零
        bool any = false;
        for (int i=0;i<4;i++) if (hn.data[i] != 0) any = true;
        if (!any) fails++;
    }
    // LSTM 形状
    {
        LSTMCell cell(3,4,2);
        int sx[2]={1,3}; fix xv[3]={fx::FX_ONE,fx::FX_HALF,0};
        Tensor x=t_from_flat(2,sx,xv);
        int sh[2]={1,4}; fix hv[4]={0,0,0,0};
        Tensor h=t_from_flat(2,sh,hv);
        Tensor c=t_from_flat(2,sh,hv);
        Tensor oc=t_zeros(2,sh);
        Tensor hn=cell.forward(x,h,c,oc);
        if (hn.shape[1]!=4) fails++;
    }
    // 序列前向
    {
        RNNCell cell(2,3,3);
        int ss[2]={5,2}; fix sv[10]={fx::FX_ONE,0,0,fx::FX_ONE,fx::FX_HALF,fx::FX_HALF,0,0,fx::FX_ONE,fx::FX_ONE};
        Tensor xs=t_from_flat(2,ss,sv);
        int sh[2]={1,3}; fix hv[3]={0,0,0};
        Tensor h0=t_from_flat(2,sh,hv);
        Tensor out=rnn_forward_sequence(cell,xs,h0);
        if (out.shape[0]!=5 || out.shape[1]!=3) fails++;
    }
    // 双向 RNN 形状
    {
        BiRNN bi(2,3,55);
        int ss[2]={4,2}; fix sv[8]={fx::FX_ONE,0,0,fx::FX_ONE,fx::FX_HALF,fx::FX_HALF,0,0};
        Tensor xs=t_from_flat(2,ss,sv);
        Tensor out=bi.forward(xs);
        if (out.shape[0]!=4 || out.shape[1]!=6) fails++;
    }
    // LSTM cell 单步（2D 输入 [1,D]）
    {
        LSTMCell lstm(3,4,7);
        fix xv[3]={fx::FX_HALF,0,fx::FX_HALF};
        fix hv[4]={0,0,0,0};
        fix cv[4]={0,0,0,0};
        Tensor x=t_from_flat(2,(int[2]){1,3},xv);
        Tensor h=t_from_flat(2,(int[2]){1,4},hv);
        Tensor c=t_from_flat(2,(int[2]){1,4},cv);
        Tensor oc=t_zeros(2,(int[2]){1,4});
        Tensor nh=lstm.forward(x,h,c,oc);
        if (nh.shape[1]!=4) fails++;
    }
    // GRU cell 单步
    {
        GRUCell gru(3,4,8);
        fix xv[3]={fx::FX_HALF,0,fx::FX_HALF};
        fix hv[4]={0,0,0,0};
        Tensor x=t_from_flat(2,(int[2]){1,3},xv);
        Tensor h=t_from_flat(2,(int[2]){1,4},hv);
        Tensor nh=gru.forward(x,h);
        if (nh.shape[1]!=4) fails++;
    }
    // RNN 序列前向：[T=3,D=2] -> [3,H=4]
    {
        RNNCell cell(2,4,9);
        fix xs[6]={fx::FX_ONE,0, 0,fx::FX_ONE, fx::FX_HALF,fx::FX_HALF};
        fix h0[4]={0,0,0,0};
        Tensor seq=t_from_flat(2,(int[2]){3,2},xs);
        Tensor hid=t_from_flat(2,(int[2]){1,4},h0);
        Tensor out=rnn_forward_sequence(cell,seq,hid);
        if (out.shape[0]!=3 || out.shape[1]!=4) fails++;
    }
    // RNNCell params 数量
    {
        RNNCell cell(4,6,1);
        List<Tensor*> ps; cell.params(ps);
        if (ps.size() != 3) fails++;  // Wxh Whh b
    }
    // LSTM params 数量
    {
        LSTMCell cell(4,6,2);
        List<Tensor*> ps; cell.params(ps);
        if (ps.size() != 3) fails++;  // Wx Wh b
    }
    // GRU params 数量
    {
        GRUCell cell(4,6,3);
        List<Tensor*> ps; cell.params(ps);
        if (ps.size() != 3) fails++;
    }
    return fails;
}


} // namespace deeplearn
} // namespace nefu
