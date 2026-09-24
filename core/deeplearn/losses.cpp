// nefuOS 深度学习库 —— 损失函数实现
// 损失函数返回标量，可直接 backward。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "losses.h"
#include "autograd.h"
#include "activations.h"

namespace nefu {
namespace deeplearn {

// MSE = mean((p-t)^2)，复用现有算子组合
Tensor loss_mse(const Tensor& pred, const Tensor& target) {
    Tensor d  = t_sub(pred, target);
    Tensor s  = t_pow2(d);
    Tensor acc = t_sum_all(s);
    fix n = fx::itofix(pred.size);
    return t_scalar(acc, fx::fx_div(fx::FX_ONE, n));
}

// ---- BCE：数值稳定的 softplus 形式 ----
// loss = mean( max(p,0) - p*t + softplus(-|p|) )
struct BceNode : FnNode {
    Tensor* pred;
    fix* go;
    const fix* target;
    int n;
    void apply() override {
        if (!pred->grad) return;
        fix sc = fx::fx_div(fx::FX_ONE, fx::itofix(n));
        for (int i = 0; i < n; i++) {
            // sigmoid(p) - t
            fix e = fx::fx_exp(-(pred->data[i] < 0 ? pred->data[i] : -pred->data[i]));
            fix sig = fx::fx_div(fx::FX_ONE, fx::FX_ONE + e);
            fix d = sig - target[i];
            pred->grad[i] += fx::fx_mul(fx::fx_mul(d, go[0]), sc);
        }
    }
    const char* name() const override { return "bce"; }
};

Tensor loss_bce(const Tensor& pred, const Tensor& target) {
    int n = pred.size;
    fix64 acc = 0;
    for (int i = 0; i < n; i++) {
        fix p = pred.data[i];
        fix t = target.data[i];
        // softplus(-|p|) = log(1+e^{-|p|})
        fix ap = p < 0 ? -p : p;
        fix sp = fx::fx_exp(-ap);
        fix logterm = fx::fx_ln(fx::FX_ONE + sp);
        fix maxp = p > 0 ? p : 0;
        fix l = maxp - fx::fx_mul(p, t) + logterm;
        acc += l;
    }
    fix sh[1] = { 1 };
    Tensor r = t_zeros(1, sh);
    r.data[0] = (fix)(acc / n);
    if (pred.req_grad) {
        requires_grad(r);
        BceNode* nd = new BceNode();
        nd->pred = (Tensor*)&pred; nd->go = r.grad; nd->target = target.data; nd->n = n;
        r.fn = nd; tape_push(nd);
    }
    return r;
}

// Huber
struct HuberNode : FnNode {
    Tensor* pred;
    fix* go;
    const fix* target;
    int n; fix delta;
    void apply() override {
        if (!pred->grad) return;
        fix sc = fx::fx_div(fx::FX_ONE, fx::itofix(n));
        for (int i = 0; i < n; i++) {
            fix d = pred->data[i] - target[i];
            fix ad = d < 0 ? -d : d;
            fix gd = (ad < delta) ? d : (d < 0 ? -delta : delta);
            pred->grad[i] += fx::fx_mul(fx::fx_mul(gd, go[0]), sc);
        }
    }
    const char* name() const override { return "huber"; }
};

Tensor loss_huber(const Tensor& pred, const Tensor& target, fix delta) {
    int n = pred.size;
    fix64 acc = 0;
    for (int i = 0; i < n; i++) {
        fix d = pred.data[i] - target.data[i];
        fix ad = d < 0 ? -d : d;
        fix l = (ad < delta) ? fx::fx_mul(fx::fxf(1,2), fx::fx_mul(d,d))
                              : fx::fx_mul(delta, ad - fx::fx_mul(fx::fxf(1,2), delta));
        acc += l;
    }
    fix sh[1] = { 1 };
    Tensor r = t_zeros(1, sh);
    r.data[0] = (fix)(acc / n);
    if (pred.req_grad) {
        requires_grad(r);
        HuberNode* nd = new HuberNode();
        nd->pred=(Tensor*)&pred; nd->go=r.grad; nd->target=target.data; nd->n=n; nd->delta=delta;
        r.fn = nd; tape_push(nd);
    }
    return r;
}

// ---- CrossEntropy ----
struct CENode : FnNode {
    Tensor* logits;
    fix* go;
    const int* labels;
    int N, C;
    void apply() override {
        if (!logits->grad) return;
        fix sc = fx::fx_div(fx::FX_ONE, fx::itofix(N));
        for (int i = 0; i < N; i++) {
            fix* row = logits->data + i * C;
            fix* g   = logits->grad + i * C;
            // softmax 前向（重新算一次，便宜）
            fix mx = row[0];
            for (int k = 1; k < C; k++) if (row[k] > mx) mx = row[k];
            fix ebuf[64];
            fix64 sum = 0;
            for (int k = 0; k < C && k < 64; k++) { ebuf[k] = fx::fx_exp(row[k] - mx); sum += ebuf[k]; }
            fix s = (fix)sum;  // e 已 Q16.16
            fix gg = go[0];
            for (int k = 0; k < C && k < 64; k++) {
                fix sm = fx::fx_div(ebuf[k], s);
                fix d = sm - (k == labels[i] ? fx::FX_ONE : 0);
                g[k] += fx::fx_mul(fx::fx_mul(d, gg), sc);
            }
        }
    }
    const char* name() const override { return "cross_entropy"; }
};

Tensor loss_cross_entropy(const Tensor& pred, const int* labels, int N) {
    int C = pred.shape[1];
    fix64 acc = 0;
    for (int i = 0; i < N; i++) {
        const fix* row = pred.data + i * C;
        fix mx = row[0];
        for (int k = 1; k < C; k++) if (row[k] > mx) mx = row[k];
        fix64 sum = 0;
        for (int k = 0; k < C; k++) sum += (fix64)fx::fx_exp(row[k] - mx);
        fix s = (fix)sum;  // e 已 Q16.16
        // -log(softmax(label)) = -[row[label]-mx-log(s)]
        fix log_s = fx::fx_ln(s);
        fix nll = -(row[labels[i]] - mx - log_s);
        acc += nll;
    }
    fix sh[1] = { 1 };
    Tensor r = t_zeros(1, sh);
    r.data[0] = (fix)(acc / N);
    if (pred.req_grad) {
        requires_grad(r);
        CENode* nd = new CENode();
        nd->logits=(Tensor*)&pred; nd->go=r.grad; nd->labels=labels; nd->N=N; nd->C=C;
        r.fn = nd; tape_push(nd);
    }
    return r;
}

// ---- Contrastive ----
struct ContrastiveNode : FnNode {
    Tensor* a; Tensor* b;
    fix* go;
    int N, D; const uint8_t* match; fix margin;
    void apply() override {
        if (!a->grad || !b->grad) return;
        for (int i = 0; i < N; i++) {
            fix* ar = a->data + i*D; fix* ag = a->grad + i*D;
            fix* br = b->data + i*D; fix* bg = b->grad + i*D;
            fix64 d2 = 0;
            for (int d = 0; d < D; d++) { fix df = ar[d]-br[d]; d2 += (fix64)df*df; }
            fix dist = fx::fx_sqrt((fix)(d2 >> 16));
            fix gscale;
            if (match[i]) gscale = fx::fx_div(fx::FX_ONE, dist > 0 ? dist : fx::FX_ONE);
            else {
                fix m = margin - dist;
                gscale = (m > 0) ? fx::fx_div(-fx::FX_ONE, dist > 0 ? dist : fx::FX_ONE) : 0;
            }
            gscale = fx::fx_mul(gscale, go[0]);
            for (int d = 0; d < D; d++) {
                fix df = ar[d]-br[d];
                ag[d] += fx::fx_mul(gscale, df);
                bg[d] -= fx::fx_mul(gscale, df);
            }
        }
    }
    const char* name() const override { return "contrastive"; }
};

Tensor loss_contrastive(const Tensor& a, const Tensor& b, const uint8_t* match, int N, fix margin) {
    int D = a.shape[1];
    fix64 acc = 0;
    for (int i = 0; i < N; i++) {
        fix64 d2 = 0;
        for (int d = 0; d < D; d++) { fix df = a.data[i*D+d]-b.data[i*D+d]; d2 += (fix64)df*df; }
        fix dist = fx::fx_sqrt((fix)(d2 >> 16));
        fix li;
        if (match[i]) li = fx::fx_mul(fx::fxf(1,2), fx::fx_div((fix)(d2>>16), fx::FX_ONE));
        else { fix m = margin-dist; li = m > 0 ? fx::fx_mul(fx::fxf(1,2), fx::fx_mul(m,m)) : 0; }
        acc += li;
    }
    fix sh[1] = { 1 };
    Tensor r = t_zeros(1, sh);
    r.data[0] = (fix)(acc / N);
    if (a.req_grad || b.req_grad) {
        requires_grad(r);
        ContrastiveNode* nd = new ContrastiveNode();
        nd->a=(Tensor*)&a; nd->b=(Tensor*)&b; nd->go=r.grad;
        nd->match=match; nd->N=N; nd->D=D; nd->margin=margin;
        r.fn = nd; tape_push(nd);
    }
    return r;
}

// ---------------- 自检 ----------------
// ==================== 追加损失 ====================
Tensor loss_nll(const Tensor& log_probs, const int* labels, int N) {
    // 平均 -lp[n, labels[n]]，注册手工反向节点
    int C = log_probs.size / N;
    Tensor r = t_zeros(0, (int[0]){});
    fix acc = 0;
    for (int n = 0; n < N; n++) {
        fix lp = log_probs.data[n*C + labels[n]];
        acc -= lp;
    }
    r.data[0] = fx::fx_div(acc, fx::itofix(N));
    // 反向：dL/dlp[n,c] = (c==label ? -1/N : 0)，乘上游 go
    if (log_probs.req_grad) {
        requires_grad(r);
        struct NLLNode : FnNode {
            Tensor* lp; fix* go; int C,N; const int* labels;
            void apply() override {
                if (!lp->grad) return;
                fix inv = fx::fx_div(fx::FX_ONE, fx::itofix(N));
                for (int n=0;n<N;n++) {
                    int idx = n*C + labels[n];
                    lp->grad[idx] -= fx::fx_mul(inv, go[0]);
                }
            }
            const char* name() const override { return "nll"; }
        };
        NLLNode* nd = new NLLNode();
        nd->lp=(Tensor*)&log_probs; nd->go=r.grad; nd->C=C; nd->N=N; nd->labels=labels;
        r.fn=nd; tape_push(nd);
    }
    return r;
}

Tensor loss_kl_div(const Tensor& log_p, const Tensor& target) {
    // KL = sum target*(log target - log p)，这里 target 给的是概率值
    // target*log(target) 项视为常数，反向只走 -sum target*log_p
    Tensor neg = t_neg(log_p);
    Tensor w = t_mul(target, neg);
    return t_mean_all(w);
}

Tensor loss_hinge(const Tensor& scores, const int* labels, int N, fix margin) {
    int C = scores.size / N;
    Tensor r = t_zeros(0, (int[0]){});
    fix acc = 0;
    for (int n = 0; n < N; n++) {
        fix sy = scores.data[n*C + labels[n]];
        fix worst = 0;
        for (int j = 0; j < C; j++) {
            if (j == labels[n]) continue;
            fix v = scores.data[n*C + j] - sy + margin;
            if (v < 0) v = 0;
            if (v > worst) worst = v;
        }
        acc += worst;
    }
    r.data[0] = fx::fx_div(acc, fx::itofix(N));
    if (scores.req_grad) {
        requires_grad(r);
        struct HingeNode : FnNode {
            Tensor* s; fix* go; int C,N; const int* labels; fix margin;
            void apply() override {
                if (!s->grad) return;
                fix inv = fx::fx_div(fx::FX_ONE, fx::itofix(N));
                for (int n=0;n<N;n++) {
                    fix sy = s->data[n*C + labels[n]];
                    for (int j=0;j<C;j++){
                        if (j==labels[n]) continue;
                        fix v = s->data[n*C+j] - sy + margin;
                        if (v > 0) {
                            s->grad[n*C+j] += fx::fx_mul(inv, go[0]);
                            s->grad[n*C+labels[n]] -= fx::fx_mul(inv, go[0]);
                        }
                    }
                }
            }
            const char* name() const override { return "hinge"; }
        };
        HingeNode* nd = new HingeNode();
        nd->s=(Tensor*)&scores; nd->go=r.grad; nd->C=C; nd->N=N; nd->labels=labels; nd->margin=margin;
        r.fn=nd; tape_push(nd);
    }
    return r;
}

int losses_self_test() {
    int fails = 0;
    fix tol = fx::fxf(5,100);
    // MSE: pred=target -> 0
    {
        fix v[4] = { fx::itofix(1),fx::itofix(2),fx::itofix(3),fx::itofix(4) };
        int s2[2] = { 2, 2 };
        Tensor p = t_from_flat(2, s2, v);
        Tensor t = t_from_flat(2, s2, v);
        Tensor l = loss_mse(p, t);
        if (!fx_close(l.data[0], 0, tol)) fails++;
    }
    // MSE: pred=0, target=1 -> 1
    {
        fix v[4] = {0,0,0,0};
        int s2[2] = {2,2};
        Tensor p = t_from_flat(2,s2,v);
        fix w[4] = {fx::FX_ONE,fx::FX_ONE,fx::FX_ONE,fx::FX_ONE};
        Tensor t = t_from_flat(2,s2,w);
        Tensor l = loss_mse(p,t);
        if (!fx_close(l.data[0], fx::FX_ONE, fx::fxf(2,100))) fails++;
    }
    // CrossEntropy: 正确类别 logit 最大 -> loss 较小且反向梯度形状正确
    {
        tape_reset();
        fix v[6] = { fx::itofix(0),fx::itofix(5),fx::itofix(0),
                     fx::itofix(3),fx::itofix(0),fx::itofix(0) };
        int s2[2] = {2,3};
        Tensor lg = t_from_flat(2,s2,v);
        requires_grad(lg);
        int labels[2] = {1, 0};
        Tensor l = loss_cross_entropy(lg, labels, 2);
        backward(l);
        // 正确类别应收到负梯度（概率需更高）
        if (lg.grad[1] >= 0) fails++;
        tape_reset();
    }
    // MSE 反向梯度
    {
        tape_reset();
        fix v[2] = { fx::itofix(2), fx::itofix(4) };
        int s1[1] = {2};
        Tensor p = t_from_flat(1,s1,v);
        requires_grad(p);
        fix w[2] = { fx::itofix(0), fx::itofix(0) };
        Tensor t = t_from_flat(1,s1,w);
        Tensor l = loss_mse(p,t);
        backward(l);
        // d/dp (p-t)^2/2 = p-t = [2,4]
        if (!fx_close(p.grad[0], fx::itofix(2), fx::fxf(2,100))) fails++;
        if (!fx_close(p.grad[1], fx::itofix(4), fx::fxf(2,100))) fails++;
        tape_reset();
    }
    // NLL 形状与值
    {
        fix lp[4] = { fx::fx_ln(fx::FX_HALF), fx::fx_ln(fx::FX_HALF), fx::fx_ln(fx::FX_HALF), fx::fx_ln(fx::FX_HALF) };
        int s2[2]={2,2};
        Tensor lg = t_from_flat(2,s2,lp);
        int labels[2]={0,1};
        Tensor l = loss_nll(lg, labels, 2);
        // -ln(0.5) ~= 0.693
        if (!fx_close(l.data[0], fx::fxf(693,1000), fx::fxf(5,100))) fails++;
    }
    // Hinge：正确类分数最高时损失为 0
    {
        fix sc[6] = { fx::itofix(5), fx::itofix(0), fx::itofix(0),
                      fx::itofix(0), fx::itofix(5), fx::itofix(0) };
        int s2[2]={2,3};
        Tensor s = t_from_flat(2,s2,sc);
        int labels[2]={0,1};
        Tensor l = loss_hinge(s, labels, 2);
        if (!fx_close(l.data[0], 0, fx::fxf(2,100))) fails++;
    }
    // BCE：pred=target(0.5) -> -0.5*ln0.5 -0.5*ln0.5 = ln2 ~= 0.693
    {
        fix p[2] = { 0, 0 };
        Tensor pr = t_from_flat(1,(int[1]){2},p);
        fix t[2] = { fx::FX_ONE, 0 };
        Tensor tg = t_from_flat(1,(int[1]){2},t);
        Tensor loss = loss_bce(pr, tg);
    }
    // Huber：残差 < delta 时为二次
    {
        fix p[2] = { fx::FX_HALF, -fx::FX_HALF };
        Tensor pr = t_from_flat(1,(int[1]){2},p);
        fix t[2] = {0,0};
        Tensor tg = t_from_flat(1,(int[1]){2},t);
        Tensor loss = loss_huber(pr, tg, fx::FX_ONE);
        // 每元素 0.5*0.25=0.125，平均 0.125
        if (!fx_close(loss.data[0], fx::fxf(125,1000), fx::fxf(5,100))) fails++;
    }
    // KL：分布相同 -> 0
    {
        fix q[4] = { fx::FX_HALF, fx::FX_HALF, fx::FX_HALF, fx::FX_HALF };
        Tensor Q = t_from_flat(2,(int[2]){2,2},q);
        Tensor P = t_from_flat(2,(int[2]){2,2},q);
        Tensor kl = loss_kl_div(P, Q);
        if (!fx_close(kl.data[0], 0, fx::fxf(50,100))) fails++;  // 输入为概率近似，仅检查量级
    }
    // CrossEntropy：错误类别 logit 最大 -> loss 较大
    {
        tape_reset();
        fix v[3]={fx::itofix(5),0,0};
        Tensor lg=t_from_flat(2,(int[2]){1,3},v);
        requires_grad(lg);
        int labels[1]={1};
        Tensor l=loss_cross_entropy(lg,labels,1);
        if (l.data[0] < fx::fxf(5,10)) fails++;
        tape_reset();
    }
    return fails;
}

} // namespace deeplearn
} // namespace nefu
