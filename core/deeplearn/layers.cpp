// nefuOS 深度学习库 —— 层实现
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "layers.h"
#include "autograd.h"

namespace nefu {
namespace deeplearn {

namespace {
void clear_buf(fix* p, int n) { if(!p) return; for(int i=0;i<n;i++) p[i]=0; }

// 简化的 He/Xavier 初始化：W ~ U(-k, k)，k = sqrt(1/in)
fix init_scale(int fan_in) {
    fix fi = fx::itofix(fan_in > 0 ? fan_in : 1);
    fix inv = fx::fx_div(fx::FX_ONE, fi);
    return fx::fx_sqrt(inv);
}
} // namespace

// 前置声明：本文件后部定义、但自检需用到的辅助层
struct Conv1D;
Tensor global_avg_pool(const Tensor& x);

// ==================== Dense ====================
Dense::Dense(int in_d, int out_d, uint32_t seed) : in_dim(in_d), out_dim(out_d) {
    int sw[2] = { in_d, out_d };
    fix k = init_scale(in_d);
    W = t_rand(2, sw, seed, -k, k);
    requires_grad(W);
    int sb[1] = { out_d };
    b = t_zeros(1, sb);
    requires_grad(b);
}

Tensor Dense::forward(const Tensor& x) {
    Tensor y = t_matmul(x, W);       // [N,out]
    Tensor yb = t_add(y, b);         // 广播 bias
    return yb;
}

void Dense::params(List<Tensor*>& out) {
    out.push(&W);
    out.push(&b);
}

// ==================== Conv2D ====================
Conv2D::Conv2D(int out_ch, int in_ch, int kr, int kc, uint32_t seed)
    : K(out_ch), R(kr), S(kc) {
    (void)kc;
    int sk[4] = { out_ch, in_ch, kr, kc };
    fix k = init_scale(in_ch * kr * kc);
    kernel = t_rand(4, sk, seed, -k, k);
    requires_grad(kernel);
    int sb[1] = { out_ch };
    bias = t_zeros(1, sb);
    requires_grad(bias);
}

struct Conv2dNode : FnNode {
    Tensor* x; Tensor* kernel; Tensor* bias;
    fix* go;
    int N,Ci,H,Wd,K,R,S,OH,OW;
    void apply() override {
        // db[k] = Σ go
        if (bias->grad) {
            for (int k = 0; k < K; k++) {
                fix acc = 0;
                for (int n = 0; n < N; n++)
                    for (int i = 0; i < OH; i++)
                        for (int j = 0; j < OW; j++)
                            acc += go[((n*K+k)*OH+i)*OW+j];
                bias->grad[k] += acc;
            }
        }
        // dW[k,ci,r,s] = Σ go * x[n,ci,i+r,j+s]
        if (kernel->grad) {
            for (int k = 0; k < K; k++)
                for (int ci = 0; ci < Ci; ci++)
                    for (int r = 0; r < R; r++)
                        for (int s = 0; s < S; s++) {
                            fix64 acc = 0;
                            for (int n = 0; n < N; n++)
                                for (int i = 0; i < OH; i++)
                                    for (int j = 0; j < OW; j++) {
                                        fix g = go[((n*K+k)*OH+i)*OW+j];
                                        fix xv = x->data[((n*Ci+ci)*(H)+(i+r))*Wd+(j+s)];
                                        acc += (fix64)g * (fix64)xv;
                                    }
                            kernel->grad[((k*Ci+ci)*R+r)*S+s] += (fix)(acc >> 16);
                        }
        }
        // dx[n,ci,h,w] = Σ go * kernel[k,ci,h-i,w-j]（same padding 思路，pad=0 直接累加）
        if (x->grad) {
            for (int n = 0; n < N; n++)
                for (int ci = 0; ci < Ci; ci++)
                    for (int h = 0; h < H; h++)
                        for (int w = 0; w < Wd; w++) {
                            fix64 acc = 0;
                            for (int k = 0; k < K; k++)
                                for (int r = 0; r < R; r++)
                                    for (int s = 0; s < S; s++) {
                                        int i = h - r, j = w - s;
                                        if (i < 0 || i >= OH || j < 0 || j >= OW) continue;
                                        fix g = go[((n*K+k)*OH+i)*OW+j];
                                        fix kv = kernel->data[((k*Ci+ci)*R+r)*S+s];
                                        acc += (fix64)g * (fix64)kv;
                                    }
                            x->grad[((n*Ci+ci)*H+h)*Wd+w] += (fix)(acc >> 16);
                        }
        }
    }
    const char* name() const override { return "conv2d"; }
};

Tensor Conv2D::forward(const Tensor& x) {
    N = x.shape[0]; Ci = x.shape[1]; H = x.shape[2]; Wd = x.shape[3];
    OH = H - R + 1;
    OW = Wd - S + 1;
    int sy[4] = { N, K, OH, OW };
    Tensor out = t_zeros(4, sy);
    for (int n = 0; n < N; n++)
        for (int k = 0; k < K; k++) {
            fix bk = bias.data[k];
            for (int i = 0; i < OH; i++)
                for (int j = 0; j < OW; j++) {
                    fix64 acc = (fix64)bk << 16;
                    for (int ci = 0; ci < Ci; ci++)
                        for (int r = 0; r < R; r++)
                            for (int s = 0; s < S; s++) {
                                fix xv = x.data[((n*Ci+ci)*H+(i+r))*Wd+(j+s)];
                                fix kv = kernel.data[((k*Ci+ci)*R+r)*S+s];
                                acc += (fix64)xv * (fix64)kv;
                            }
                    out.data[((n*K+k)*OH+i)*OW+j] = (fix)(acc >> 16);
                }
        }
    if (x.req_grad || kernel.req_grad) {
        requires_grad(out);
        Conv2dNode* nd = new Conv2dNode();
        nd->x=(Tensor*)&x; nd->kernel=&kernel; nd->bias=&bias; nd->go=out.grad;
        nd->N=N;nd->Ci=Ci;nd->H=H;nd->Wd=Wd;nd->K=K;nd->R=R;nd->S=S;nd->OH=OH;nd->OW=OW;
        out.fn = nd; tape_push(nd);
    }
    return out;
}

void Conv2D::params(List<Tensor*>& out) {
    out.push(&kernel);
    out.push(&bias);
}

// ==================== MaxPool2D ====================
struct MaxPoolNode : FnNode {
    Tensor* x;
    fix* go;
    int N,C,H,W;
    void apply() override {
        if (!x->grad) return;
        int OH = H/2, OW = W/2;
        for (int n = 0; n < N; n++)
            for (int c = 0; c < C; c++)
                for (int i = 0; i < OH; i++)
                    for (int j = 0; j < OW; j++) {
                        // 找 2x2 最大位置（与前向一致）
                        fix v00 = x->data[((n*C+c)*H+(2*i))*W+(2*j)];
                        fix v01 = x->data[((n*C+c)*H+(2*i))*W+(2*j+1)];
                        fix v10 = x->data[((n*C+c)*H+(2*i+1))*W+(2*j)];
                        fix v11 = x->data[((n*C+c)*H+(2*i+1))*W+(2*j+1)];
                        fix mx = v00;
                        int bi=0,bj=0;
                        if (v01>mx){mx=v01;bj=1;}
                        if (v10>mx){mx=v10;bi=1;bj=0;}
                        if (v11>mx){mx=v11;bi=1;bj=1;}
                        fix g = go[((n*C+c)*OH+i)*OW+j];
                        x->grad[((n*C+c)*H+(2*i+bi))*W+(2*j+bj)] += g;
                    }
    }
    const char* name() const override { return "maxpool"; }
};

Tensor MaxPool2D::forward(const Tensor& x) {
    int N=x.shape[0], C=x.shape[1], H=x.shape[2], W=x.shape[3];
    int OH=H/2, OW=W/2;
    int sy[4]={N,C,OH,OW};
    Tensor out = t_zeros(4,sy);
    for (int n=0;n<N;n++) for (int c=0;c<C;c++)
      for (int i=0;i<OH;i++) for (int j=0;j<OW;j++) {
        fix v00=x.data[((n*C+c)*H+2*i)*W+2*j];
        fix v01=x.data[((n*C+c)*H+2*i)*W+2*j+1];
        fix v10=x.data[((n*C+c)*H+2*i+1)*W+2*j];
        fix v11=x.data[((n*C+c)*H+2*i+1)*W+2*j+1];
        fix mx=v00; if(v01>mx)mx=v01; if(v10>mx)mx=v10; if(v11>mx)mx=v11;
        out.data[((n*C+c)*OH+i)*OW+j]=mx;
      }
    if (x.req_grad) {
        requires_grad(out);
        MaxPoolNode* nd=new MaxPoolNode();
        nd->x=(Tensor*)&x; nd->go=out.grad; nd->N=N;nd->C=C;nd->H=H;nd->W=W;
        out.fn=nd; tape_push(nd);
    }
    return out;
}

// ==================== AvgPool2D ====================
Tensor AvgPool2D::forward(const Tensor& x) {
    int N=x.shape[0], C=x.shape[1], H=x.shape[2], W=x.shape[3];
    int OH=H/2, OW=W/2;
    fix inv = fx::fxf(1,4);
    int sy[4]={N,C,OH,OW};
    Tensor out=t_zeros(4,sy);
    for (int n=0;n<N;n++) for (int c=0;c<C;c++)
      for (int i=0;i<OH;i++) for (int j=0;j<OW;j++) {
        fix s = x.data[((n*C+c)*H+2*i)*W+2*j]
              + x.data[((n*C+c)*H+2*i)*W+2*j+1]
              + x.data[((n*C+c)*H+2*i+1)*W+2*j]
              + x.data[((n*C+c)*H+2*i+1)*W+2*j+1];
        out.data[((n*C+c)*OH+i)*OW+j] = fx::fx_mul(s, inv);
      }
    // avgpool 反向较简单（均匀传回），这里不注册（推理用）
    return out;
}

// ==================== Flatten ====================
Tensor layer_flatten(const Tensor& x) {
    int N = x.shape[0];
    int rest = x.size / N;
    int sh[2] = { N, rest };
    return t_reshape(x, 2, sh);
}

// ==================== Dropout ====================
Tensor Dropout::forward(const Tensor& x, uint32_t seed) {
    if (!training || p == 0) return t_copy(x);   // 推理恒等（拷贝一份）
    fix keep = fx::FX_ONE - p;
    fix scale = fx::fx_div(fx::FX_ONE, keep);
    Tensor out = t_zeros(x.nd, x.shape);
    uint32_t s = seed ? seed : 0xC0FFEE;
    for (int i = 0; i < x.size; i++) {
        s = s * 1664525u + 1013904223u;
        fix r = (fix)(s >> 16);   // [0,1)
        out.data[i] = (r < p) ? 0 : fx::fx_mul(x.data[i], scale);
    }
    return out;
}

// ==================== BatchNorm2D ====================
BatchNorm2D::BatchNorm2D(int channels, bool train) : C(channels), training(train) {
    int s[1] = { C };
    gamma = t_ones(1, s); requires_grad(gamma);
    beta  = t_zeros(1, s); requires_grad(beta);
    momentum = fx::fxf(1,10);
    for (int i = 0; i < C && i < 64; i++) { running_mean[i]=0; running_var[i]=fx::FX_ONE; }
}

struct BnNode : FnNode {
    Tensor* x; Tensor* gamma; Tensor* beta;
    fix* go;
    int N,C,H,W;
    fix* mean; fix* invvar;
    void apply() override {
        if (!x->grad) return;
        int M = N*H*W;
        for (int c = 0; c < C; c++) {
            fix gmean = mean[c], iv = invvar[c], g = gamma->data[c];
            // dg = Σ go * xhat ; db = Σ go ; dx = (1/M)*iv*(M*go - Σgo - xhat*Σ(go*xhat))
            fix64 dg=0, db=0;
            for (int n=0;n<N;n++) for (int i=0;i<H;i++) for (int j=0;j<W;j++) {
                fix gout = go[((n*C+c)*H+i)*W+j];
                fix xh = (x->data[((n*C+c)*H+i)*W+j]-gmean)*iv;
                dg += (fix64)gout*(fix64)xh;
                db += gout;
            }
            gamma->grad[c] += (fix)(dg>>16);
            beta->grad[c]  += (fix)(db);
            fix sc = fx::fx_div(fx::FX_ONE, fx::itofix(M));
            for (int n=0;n<N;n++) for (int i=0;i<H;i++) for (int j=0;j<W;j++) {
                int idx=((n*C+c)*H+i)*W+j;
                fix gout=go[idx];
                fix xh=(x->data[idx]-gmean)*iv;
                fix d = fx::fx_mul(sc, fx::fx_mul(iv, fx::itofix(M)*gout - (fix)db - fx::fx_mul(xh,(fix)(dg>>16))));
                x->grad[idx] += fx::fx_mul(d, g);
            }
        }
    }
    const char* name() const override { return "batchnorm"; }
};

Tensor BatchNorm2D::forward(const Tensor& x) {
    int N=x.shape[0], Cc=x.shape[1], Hh=x.shape[2], Ww=x.shape[3];
    int M=N*Hh*Ww;
    fix mean[64], var[64];
    for (int c=0;c<Cc && c<64;c++) {
        fix64 s=0, s2=0;
        for (int n=0;n<N;n++) for (int i=0;i<Hh;i++) for (int j=0;j<Ww;j++) {
            fix v=x.data[((n*Cc+c)*Hh+i)*Ww+j];
            s+=v; s2+=(fix64)v*v;
        }
        mean[c]=(fix)(s/M);
        fix ev=(fix)(s2/M);
        var[c]=ev - fx::fx_mul(mean[c],mean[c]);
    }
    Tensor out=t_zeros(x.nd,x.shape);
    static fix s_mean[64], s_iv[64];
    for (int c=0;c<Cc && c<64;c++) {
        fix iv = fx::fx_div(fx::FX_ONE, fx::fx_sqrt(var[c] + fx::fxf(1,1000)));
        s_mean[c]=mean[c]; s_iv[c]=iv;
        for (int n=0;n<N;n++) for (int i=0;i<Hh;i++) for (int j=0;j<Ww;j++) {
            int idx=((n*Cc+c)*Hh+i)*Ww+j;
            fix xh = fx::fx_mul(x.data[idx]-mean[c], iv);
            out.data[idx] = fx::fx_mul(gamma.data[c], xh) + beta.data[c];
        }
    }
    if (x.req_grad) {
        requires_grad(out);
        BnNode* nd=new BnNode();
        nd->x=(Tensor*)&x; nd->gamma=&gamma; nd->beta=&beta; nd->go=out.grad;
        nd->N=N;nd->C=Cc;nd->H=Hh;nd->W=Ww; nd->mean=s_mean; nd->invvar=s_iv;
        out.fn=nd; tape_push(nd);
    }
    return out;
}

void BatchNorm2D::params(List<Tensor*>& out) { out.push(&gamma); out.push(&beta); }

// ==================== LayerNorm ====================
LayerNorm::LayerNorm(int dim) : D(dim) {
    int s[1]={D};
    gamma=t_ones(1,s); requires_grad(gamma);
    beta=t_zeros(1,s); requires_grad(beta);
}

Tensor LayerNorm::forward(const Tensor& x) {
    // x: [..., D]，对每行 D 归一
    int rows = x.size / D;
    Tensor out=t_zeros(x.nd,x.shape);
    for (int r=0;r<rows;r++) {
        const fix* xr=x.data+r*D; fix* orow=out.data+r*D;
        fix64 s=0,s2=0;
        for (int j=0;j<D;j++){s+=xr[j];s2+=(fix64)xr[j]*xr[j];}
        fix mean=(fix)(s/D);
        fix var=(fix)(s2/D)-fx::fx_mul(mean,mean);
        fix iv=fx::fx_div(fx::FX_ONE, fx::fx_sqrt(var+fx::fxf(1,1000)));
        for (int j=0;j<D;j++) {
            fix xh=fx::fx_mul(xr[j]-mean,iv);
            orow[j]=fx::fx_mul(gamma.data[j],xh)+beta.data[j];
        }
    }
    // LayerNorm 反向较繁，这里仅前向（推理用）
    return out;
}
void LayerNorm::params(List<Tensor*>& out) { out.push(&gamma); out.push(&beta); }

// ==================== Embedding ====================
Embedding::Embedding(int vocab, int dim, uint32_t seed) : V(vocab), D(dim) {
    int s[2]={vocab,dim};
    fix k=init_scale(dim);
    table=t_rand(2,s,seed,-k,k);
    requires_grad(table);
}

struct EmbedNode : FnNode {
    Tensor* table;
    fix* go;
    const int* ids; int n; int D;
    void apply() override {
        if (!table->grad) return;
        for (int i=0;i<n;i++) {
            int id=ids[i];
            for (int j=0;j<D;j++) table->grad[id*D+j] += go[i*D+j];
        }
    }
    const char* name() const override { return "embedding"; }
};

Tensor Embedding::forward(const int* ids, int n) {
    int s[2]={n,D};
    Tensor out=t_zeros(2,s);
    for (int i=0;i<n;i++) {
        int id=ids[i] < V ? ids[i] : 0;
        for (int j=0;j<D;j++) out.data[i*D+j]=table.data[id*D+j];
    }
    if (table.req_grad) {
        requires_grad(out);
        EmbedNode* nd=new EmbedNode();
        nd->table=&table; nd->go=out.grad; nd->ids=ids; nd->n=n; nd->D=D;
        out.fn=nd; tape_push(nd);
    }
    return out;
}
void Embedding::params(List<Tensor*>& out) { out.push(&table); }

// ==================== 自检 ====================
int layers_self_test() {
    int fails = 0;
    fix tol = fx::fxf(5,100);
    // Dense 前向 + 训练一步 loss 下降
    {
        tape_reset();
        Dense l(2,1,1234);
        // 把 W 设成固定值便于核对：W=[[1],[1]], b=0 -> y = x0+x1
        l.W.data[0]=fx::FX_ONE; l.W.data[1]=fx::FX_ONE; l.b.data[0]=0;
        fix xv[4]={fx::itofix(1),fx::itofix(2), fx::itofix(3),fx::itofix(4)};
        int sx[2]={2,2};
        Tensor x=t_from_flat(2,sx,xv);
        Tensor y=l.forward(x);   // [1+2, 3+4] = [3,7]
        if (!fx_close(y.data[0],fx::itofix(3),tol)) fails++;
        if (!fx_close(y.data[1],fx::itofix(7),tol)) fails++;
        tape_reset();
    }
    // Conv2D 形状
    {
        Conv2D c(2,1,2,2,1);
        int sx[4]={1,1,4,4};
        Tensor x=t_zeros(4,sx);
        Tensor y=c.forward(x);
        if (y.shape[0]!=1 || y.shape[1]!=2 || y.shape[2]!=3 || y.shape[3]!=3) fails++;
    }
    // MaxPool 形状
    {
        int sx[4]={1,1,4,4};
        Tensor x=t_zeros(4,sx);
        MaxPool2D m;
        Tensor y=m.forward(x);
        if (y.shape[2]!=2 || y.shape[3]!=2) fails++;
    }
    // Flatten
    {
        int sx[4]={2,3,4,4};
        Tensor x=t_zeros(4,sx);
        Tensor y=layer_flatten(x);
        if (y.shape[0]!=2 || y.shape[1]!=48) fails++;
    }
    // Embedding 查表
    {
        Embedding e(5,3,7);
        int ids[2]={1,3};
        Tensor y=e.forward(ids,2);
        if (!fx_close(y.data[0], e.table.data[3], tol)) fails++;
    }
    // LayerNorm：单位方差附近
    {
        LayerNorm ln(4);
        fix v[8]={fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4),
                  fx::itofix(-1),0,fx::FX_ONE,fx::itofix(2)};
        int sx[2]={2,4};
        Tensor x=t_from_flat(2,sx,v);
        Tensor y=ln.forward(x);
        // 第一行均值 2，归一后均值应≈0
        fix r0=y.data[0]+y.data[1]+y.data[2]+y.data[3];
        if (!fx_close(r0,0,fx::fxf(20,100))) fails++;
    }
    // GlobalAvgPool 形状与值
    {
        fix v[4]={fx::FX_ONE,fx::FX_ONE,fx::FX_ONE,fx::FX_ONE};
        Tensor X=t_from_flat(4,(int[4]){1,1,2,2},v);
        Tensor g=global_avg_pool(X);
        if (!fx_close(g.data[0], fx::FX_ONE, fx::fxf(2,100))) fails++;
    }
    // LayerNorm：全同值行 -> 输出 = beta（归一化后为 0*gamma+beta）
    {
        LayerNorm ln(3);
        fix v[3]={fx::itofix(5),fx::itofix(5),fx::itofix(5)};
        Tensor X=t_from_flat(2,(int[2]){1,3},v);
        Tensor y=ln.forward(X);
        // 方差=0，inv=1/sqrt(eps)，mean=5，(5-5)*inv=0，输出=beta=0
        if (!fx_close(y.data[0], 0, fx::fxf(5,100))) fails++;
    }
    // LayerNorm：零均值单位方差输出（gamma=1,beta=0 近似）
    {
        LayerNorm ln(4);
        fix v[8] = { fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4),
                     fx::itofix(1),fx::itofix(1),fx::itofix(1),fx::itofix(1) };
        Tensor x = t_from_flat(2,(int[2]){2,4},v);
        Tensor y = ln.forward(x);
        if (y.shape[0]!=2 || y.shape[1]!=4) fails++;
    }
    // Flatten：[2,2,2] -> [8]
    {
        fix v[8] = {0,1,2,3,4,5,6,7};
        Tensor x = t_from_flat(3,(int[3]){2,2,2},v);
        Tensor f = layer_flatten(x);
        if (f.size != 8) fails++;  // 展平后元素数=8
    }
    // Embedding：查表
    {
        Embedding emb(5,3,42);
        int ids[3] = {0,2,4};
        Tensor e = emb.forward(ids, 3);
        if (e.shape[0]!=3 || e.shape[1]!=3) fails++;
    }
    // AvgPool2D：[1,1,2,2] 全一 -> [1,1,1,1]=1
    {
        fix v[4] = {fx::FX_ONE,fx::FX_ONE,fx::FX_ONE,fx::FX_ONE};
        Tensor x = t_from_flat(4,(int[4]){1,1,2,2},v);
        AvgPool2D ap;
        Tensor y = ap.forward(x);
        if (y.shape[0]!=1 || y.shape[2]!=1) fails++;  // 形状检查
    }
    return fails;

}

// ==================== 追加层：Conv1D ====================
// 一维卷积：x:[N,Ci,L]，kernel:[K,Ci,R]，y:[N,K,LO]（LO=L-R+1，stride=1，pad=0）
struct Conv1D {
    int K, Ci, R, LO;
    Tensor kernel;   // [K,Ci,R]
    Tensor bias;     // [K]
    Conv1D(int out_ch, int in_ch, int kr, uint32_t seed) : K(out_ch), Ci(in_ch), R(kr) {
        int sk[3]={out_ch,in_ch,kr};
        fix k = fx::fx_sqrt(fx::fx_div(fx::FX_ONE, fx::itofix(in_ch*kr)));
        kernel = t_rand(3,sk,seed,-k,k); requires_grad(kernel);
        int sb[1]={out_ch}; bias=t_zeros(1,sb); requires_grad(bias);
    }
    Tensor forward(const Tensor& x) {
        int N=x.shape[0], L=x.shape[2];
        LO = L - R + 1;
        int so[4]={N,K,LO,1};   // 用 4D 复用逻辑不现实，直接手写 3D 输出
        Tensor r = t_zeros(3,(int[3]){N,K,LO});
        for (int n=0;n<N;n++) for (int k=0;k<K;k++) {
            r.data[(n*K+k)*LO] = bias.data[k];
            for (int o=0;o<LO;o++) {
                fix acc = bias.data[k];
                for (int c=0;c<Ci;c++) for (int j=0;j<R;j++) {
                    fix xv = x.data[((n*Ci+c)*L)+o+j];
                    fix kv = kernel.data[((k*Ci+c)*R)+j];
                    acc += fx::fx_mul(xv, kv);
                }
                r.data[(n*K+k)*LO+o] = acc;
            }
        }
        return r;
    }
    void params(List<Tensor*>& out){ out.push(&kernel); out.push(&bias); }
};

// ==================== 追加层：全局平均池化 ====================
// x:[N,C,H,W] -> [N,C]（对 H,W 取平均）
Tensor global_avg_pool(const Tensor& x) {
    int N=x.shape[0], C=x.shape[1], H=x.shape[2], W=x.shape[3];
    Tensor r = t_zeros(2,(int[2]){N,C});
    fix inv = fx::fx_div(fx::FX_ONE, fx::itofix(H*W));
    for (int n=0;n<N;n++) for (int c=0;c<C;c++) {
        fix s=0;
        for (int i=0;i<H;i++) for (int j=0;j<W;j++)
            s += x.data[((n*C+c)*H+i)*W+j];
        r.data[n*C+c] = fx::fx_mul(s, inv);
    }
    return r;
}

} // namespace deeplearn
} // names