// nefuOS 深度学习库 —— 激活函数实现
// 全部激活函数定点实现，自动挂反向节点。
// 内存：new[]/delete[]，禁 STL；定点 Q16.16；无异常/RTTI。
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "activations.h"
#include "autograd.h"

namespace nefu {
namespace deeplearn {

namespace {
// 逐元素一元节点通用骨架：保存输入指针与上游梯度
struct UnaryNode : FnNode {
    Tensor* in;
    fix* go;
    fix* out;       // 输出数据堆缓冲（前向结果，反向用）
    int n;
};

struct ReluNode : UnaryNode {
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++)
            if (in->data[i] > 0) in->grad[i] += go[i];
    }
    const char* name() const override { return "relu"; }
};

struct LeakyReluNode : UnaryNode {
    fix slope;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix g = (in->data[i] > 0) ? go[i] : fx::fx_mul(slope, go[i]);
            in->grad[i] += g;
        }
    }
    const char* name() const override { return "leaky_relu"; }
};

struct EluNode : UnaryNode {
    fix alpha;
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix g = (in->data[i] > 0) ? go[i] : fx::fx_mul(alpha, out[i]);
            in->grad[i] += g;
        }
    }
    const char* name() const override { return "elu"; }
};

struct SigmoidNode : UnaryNode {
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix o = out[i];
            in->grad[i] += fx::fx_mul(fx::fx_mul(o, fx::FX_ONE - o), go[i]);
        }
    }
    const char* name() const override { return "sigmoid"; }
};

struct TanhNode : UnaryNode {
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix o = out[i];
            in->grad[i] += fx::fx_mul(fx::FX_ONE - fx::fx_mul(o, o), go[i]);
        }
    }
    const char* name() const override { return "tanh"; }
};

struct SwishNode : UnaryNode {
    void apply() override {
        if (!in->grad) return;
        for (int i = 0; i < n; i++) {
            fix o = out[i];
            fix xv = in->data[i];
            fix s = xv != 0 ? fx::fx_div(o, xv) : fx::FX_HALF;  // sig(x)
            fix d = s + fx::fx_mul(o, fx::FX_ONE - s);
            in->grad[i] += fx::fx_mul(d, go[i]);
        }
    }
    const char* name() const override { return "swish"; }
};

fix relu_f(fix x)    { return x > 0 ? x : 0; }
fix sigmoid_f(fix x) { fix e = fx::fx_exp(-x); return fx::fx_div(fx::FX_ONE, fx::FX_ONE + e); }
fix tanh_f(fix x) {
    fix twox = (fix)((fix64)x << 1);
    fix e = fx::fx_exp(twox);
    return fx::fx_div(e - fx::FX_ONE, e + fx::FX_ONE);
}

// 注册一元节点的公共收尾
template <typename NodeT>
void hook_unary(const Tensor& x, Tensor& r, NodeT* n) {
    requires_grad(r);
    n->in = (Tensor*)&x; n->go = r.grad; n->out = r.data; n->n = x.size;
    r.fn = n; tape_push(n);
}

} // namespace

Tensor act_relu(const Tensor& x) {
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++) r.data[i] = relu_f(x.data[i]);
    if (x.req_grad) { ReluNode* n = new ReluNode(); hook_unary(x, r, n); }
    return r;
}

Tensor act_leaky_relu(const Tensor& x, fix slope) {
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++)
        r.data[i] = (x.data[i] > 0) ? x.data[i] : fx::fx_mul(slope, x.data[i]);
    if (x.req_grad) {
        LeakyReluNode* n = new LeakyReluNode();
        hook_unary(x, r, n); n->slope = slope;
    }
    return r;
}

Tensor act_elu(const Tensor& x, fix alpha) {
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++)
        r.data[i] = (x.data[i] > 0) ? x.data[i] : fx::fx_mul(alpha, fx::fx_exp(x.data[i]) - fx::FX_ONE);
    if (x.req_grad) {
        EluNode* n = new EluNode();
        hook_unary(x, r, n); n->alpha = alpha;
    }
    return r;
}

Tensor act_sigmoid(const Tensor& x) {
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++) r.data[i] = sigmoid_f(x.data[i]);
    if (x.req_grad) { SigmoidNode* n = new SigmoidNode(); hook_unary(x, r, n); }
    return r;
}

Tensor act_tanh(const Tensor& x) {
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++) r.data[i] = tanh_f(x.data[i]);
    if (x.req_grad) { TanhNode* n = new TanhNode(); hook_unary(x, r, n); }
    return r;
}

Tensor act_swish(const Tensor& x) {
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++)
        r.data[i] = fx::fx_mul(x.data[i], sigmoid_f(x.data[i]));
    if (x.req_grad) { SwishNode* n = new SwishNode(); hook_unary(x, r, n); }
    return r;
}

Tensor act_gelu(const Tensor& x) {
    fix k = fx::fxf(1702, 1000);
    Tensor r = t_zeros(x.nd, x.shape);
    for (int i = 0; i < x.size; i++)
        r.data[i] = fx::fx_mul(x.data[i], sigmoid_f(fx::fx_mul(k, x.data[i])));
    if (x.req_grad) { SwishNode* n = new SwishNode(); hook_unary(x, r, n); }
    return r;
}

// ---- softmax 沿最后一维 ----
struct SoftmaxNode : FnNode {
    Tensor* in;
    fix* go;
    fix* out;
    int rowlen;
    int nrows;
    void apply() override {
        if (!in->grad) return;
        for (int ri = 0; ri < nrows; ri++) {
            fix* orow = out + ri * rowlen;
            fix* grow = go + ri * rowlen;
            fix64 s = 0;
            for (int k = 0; k < rowlen; k++) s += (fix64)orow[k] * (fix64)grow[k];
            fix sc = (fix)(s >> 16);
            for (int k = 0; k < rowlen; k++)
                in->grad[ri * rowlen + k] += fx::fx_mul(orow[k], grow[k] - sc);
        }
    }
    const char* name() const override { return "softmax"; }
};

Tensor act_softmax(const Tensor& x) {
    Tensor r = t_zeros(x.nd, x.shape);
    int rowlen = x.shape[x.nd - 1];
    int nrows = x.size / rowlen;
    for (int ri = 0; ri < nrows; ri++) {
        const fix* xr = x.data + ri * rowlen;
        fix* orow = r.data + ri * rowlen;
        fix mx = xr[0];
        for (int k = 1; k < rowlen; k++) if (xr[k] > mx) mx = xr[k];
        fix64 sum = 0;
        for (int k = 0; k < rowlen; k++) {
            fix e = fx::fx_exp(xr[k] - mx);
            orow[k] = e;
            sum += (fix64)e;
        }
        fix s = (fix)sum;  // e 已是 Q16.16，求和不再右移
        for (int k = 0; k < rowlen; k++)
            orow[k] = fx::fx_div(orow[k], s);
    }
    if (x.req_grad) {
        requires_grad(r);
        SoftmaxNode* n = new SoftmaxNode();
        n->in = (Tensor*)&x; n->go = r.grad; n->out = r.data;
        n->rowlen = rowlen; n->nrows = nrows;
        r.fn = n; tape_push(n);
    }
    return r;
}

// ---------------- 自检 ----------------
int activations_self_test() {
    int fails = 0;
    fix tol = fx::fxf(2, 100);
    {
        fix v[3] = { fx::itofix(-1), 0, fx::itofix(2) };
        int s1[1] = { 3 };
        Tensor x = t_from_flat(1, s1, v);
        Tensor y = act_relu(x);
        if (!fx_close(y.data[0], 0, tol)) fails++;
        if (!fx_close(y.data[2], fx::itofix(2), tol)) fails++;
    }
    {
        fix v[1] = { 0 };
        int s1[1] = { 1 };
        Tensor x = t_from_flat(1, s1, v);
        Tensor y = act_sigmoid(x);
        if (!fx_close(y.data[0], fx::FX_HALF, fx::fxf(5,100))) fails++;
    }
    {
        fix v[1] = { 0 };
        int s1[1] = { 1 };
        Tensor x = t_from_flat(1, s1, v);
        Tensor y = act_tanh(x);
        if (!fx_close(y.data[0], 0, fx::fxf(2,100))) fails++;
    }
    {
        fix v[6] = { fx::itofix(1),fx::itofix(2),fx::itofix(3), fx::itofix(-1),fx::itofix(0),fx::itofix(1) };
        int s2[2] = { 2, 3 };
        Tensor x = t_from_flat(2, s2, v);
        Tensor y = act_softmax(x);
        fix r0 = y.data[0]+y.data[1]+y.data[2];
        fix r1 = y.data[3]+y.data[4]+y.data[5];
        if (!fx_close(r0, fx::FX_ONE, fx::fxf(5,100))) fails++;
        if (!fx_close(r1, fx::FX_ONE, fx::fxf(5,100))) fails++;
    }
    {
        tape_reset();
        fix v[3] = { fx::itofix(-1), fx::itofix(1), fx::itofix(2) };
        int s1[1] = { 3 };
        Tensor x = t_from_flat(1, s1, v);
        requires_grad(x);
        Tensor y = act_relu(x);
        Tensor loss = t_sum_all(y);
        backward(loss);
        if (!fx_close(x.grad[0], 0, tol)) fails++;
        if (!fx_close(x.grad[1], fx::FX_ONE, tol)) fails++;
        if (!fx_close(x.grad[2], fx::FX_ONE, tol)) fails++;
        tape_reset();
    }
    {
        // hardswish: x=3 -> x*relu6(6)/6 = 3*6/6=3
        fix v[1] = { fx::itofix(3) }; int s1[1]={1};
        Tensor x = t_from_flat(1,s1,v);
        Tensor y = act_hardswish(x);
        if (!fx_close(y.data[0], fx::itofix(3), fx::fxf(5,100))) fails++;
    }
    {
        // softplus(0) = ln(2) ~= 0.693
        fix v[1] = { 0 }; int s1[1]={1};
        Tensor x = t_from_flat(1,s1,v);
        Tensor y = act_softplus(x);
        if (!fx_close(y.data[0], fx::fxf(693,1000), fx::fxf(5,100))) fails++;
    }
    {
        // mish(0) = 0
        fix v[1] = { 0 }; int s1[1]={1};
        Tensor x = t_from_flat(1,s1,v);
        Tensor y = act_mish(x);
        if (!fx_close(y.data[0], 0, fx::fxf(3,100))) fails++;
    }
    // ReLU 反向：正输入梯度直通，负输入为 0
    {
        tape_reset();
        fix v[2] = { fx::FX_ONE, -fx::FX_HALF };
        Tensor x = t_from_flat(1,(int[1]){2},v);
        requires_grad(x);
        Tensor y = act_relu(x);
        Tensor s = t_sum_all(y);
        backward(s);
        if (!fx_close(x.grad[0], fx::FX_ONE, fx::fxf(2,100))) fails++;
        if (!fx_close(x.grad[1], 0, fx::fxf(2,100))) fails++;
        tape_reset();
    }
    // Sigmoid 输出在 (0,1) 之间
    {
        fix v[2] = { fx::itofix(2), -fx::itofix(2) };
        Tensor x = t_from_flat(1,(int[1]){2},v);
        Tensor y = act_sigmoid(x);
        if (y.data[0] <= 0 || y.data[0] >= fx::FX_ONE) fails++;
        if (y.data[1] <= 0 || y.data[1] >= fx::FX_ONE) fails++;
    }
    // Hardswish(0)=0, Hardswish(3)=3
    {
        fix v[2] = { 0, fx::itofix(3) };
        Tensor x = t_from_flat(1,(int[1]){2},v);
        Tensor y = act_hardswish(x);
        if (!fx_close(y.data[0], 0, fx::fxf(2,100))) fails++;
        if (!fx_close(y.data[1], fx::itofix(3), fx::fxf(5,100))) fails++;
    }
    // GELU(0)=0
    {
        fix v[1] = { 0 };
        Tensor x = t_from_flat(1,(int[1]){1},v);
        Tensor y = act_gelu(x);
        if (!fx_close(y.data[0], 0, fx::fxf(5,100))) fails++;
    }
    // LeakyReLU：正段直通，负段斜率 0.01
    {
        fix v[2]={fx::FX_ONE, -fx::FX_ONE};
        Tensor x=t_from_flat(1,(int[1]){2},v);
        Tensor y=act_leaky_relu(x);
        if (!fx_close(y.data[0], fx::FX_ONE, fx::fxf(2,100))) fails++;
        if (y.data[1] >= 0) fails++;  // 负段保留负值
    }
    // ELU(0)=0
    {
        fix v[1]={0};
        Tensor x=t_from_flat(1,(int[1]){1},v);
        Tensor y=act_elu(x);
        if (!fx_close(y.data[0], 0, fx::fxf(2,100))) fails++;
    }
    // Swish(0)=0
    {
        fix v[1]={0};
        Tensor x=t_from_flat(1,(int[1]){1},v);
        Tensor y=act_swish(x);
        if (!fx_close(y.data[0], 0, fx::fxf(2,100))) fails++;
    }
    // Softmax 每行和为 1
    {
        fix v[6]={fx::itofix(1),fx::itofix(2),fx::itofix(3), 0,0,0};
        Tensor x=t_from_flat(2,(int[2]){2,3},v);
        Tensor y=act_softmax(x);
        fix s0=y.data[0]+y.data[1]+y.data[2];
        fix s1=y.data[3]+y.data[4]+y.data[5];
        if (!fx_close(s0, fx::FX_ONE, fx::fxf(5,100))) fails++;
        if (!fx_close(s1, fx::FX_ONE, fx::fxf(5,100))) fails++;
    }
    return fails;
}

// ==================== 追加激活（用现有算子组合，自动带反向） ====================
Tensor act_hardswish(const Tensor& x) {
    Tensor three = t_full(x.nd, x.shape, fx::itofix(3));
    Tensor x3 = t_add(x, three);
    Tensor r6 = t_clip(x3, 0, fx::itofix(6));
    Tensor up = t_mul(x, r6);
    return t_scalar(up, fx::fx_div(fx::FX_ONE, fx::itofix(6)));
}

Tensor act_softplus(const Tensor& x) {
    Tensor ex = t_exp(x);
    Tensor one = t_full(x.nd, x.shape, fx::FX_ONE);
    return t_log(t_add(ex, one));
}

Tensor act_mish(const Tensor& x) {
    Tensor sp = act_softplus(x);
    Tensor th = act_tanh(sp);
    return t_mul(x, th);
}

} // namespace deeplearn
} // namespace nefu

